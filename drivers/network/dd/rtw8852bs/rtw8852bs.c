/*
 * PROJECT:     LiberNT Realtek RTL8852BS Native 802.11 Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Driver entry, miniport lifecycle and transmit queue
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "rtw8852bs.h"

#define NDEBUG
#include <debug.h>

#define RTW_INITIAL_CHANNEL             1

static NDIS_HANDLE RtwMiniportDriverHandle;

VOID
RtwWakeCommand(
    _In_ PRTW_ADAPTER Adapter)
{
    KeSetEvent(&Adapter->CommandEvent, IO_NETWORK_INCREMENT, FALSE);
}

static
VOID
RtwCompleteNbls(
    _In_ PRTW_ADAPTER Adapter,
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

static
PNET_BUFFER_LIST
RtwDequeueNbl(
    _In_ PRTW_ADAPTER Adapter)
{
    PNET_BUFFER_LIST Nbl;

    NdisAcquireSpinLock(&Adapter->TxQueueLock);
    Nbl = Adapter->TxQueueHead;
    if (Nbl)
    {
        Adapter->TxQueueHead = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        if (!Adapter->TxQueueHead)
            Adapter->TxQueueTail = NULL;
        NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;
    }
    NdisReleaseSpinLock(&Adapter->TxQueueLock);

    return Nbl;
}

VOID
RtwTxPump(
    _In_ PRTW_ADAPTER Adapter)
{
    PDOT11_EXTSTA_SEND_CONTEXT SendContext;
    PNET_BUFFER_LIST Nbl;
    PNET_BUFFER NetBuffer;
    NDIS_STATUS Status;
    BOOLEAN Exempt;

    while ((Nbl = RtwDequeueNbl(Adapter)) != NULL)
    {
        Status = NDIS_STATUS_SUCCESS;
        if (Adapter->LinkState != RtwLinkAssociated || !Adapter->DataPathRunning || Adapter->Halting)
        {
            Status = NDIS_STATUS_MEDIA_DISCONNECTED;
        }
        else
        {
            SendContext = NET_BUFFER_LIST_INFO(Nbl, MediaSpecificInformation);
            Exempt = SendContext && SendContext->usExemptionActionType == DOT11_EXEMPT_ALWAYS;

            for (NetBuffer = NET_BUFFER_LIST_FIRST_NB(Nbl); NetBuffer; NetBuffer = NET_BUFFER_NEXT_NB(NetBuffer))
            {
                if (NT_SUCCESS(RtwSendNetBuffer(Adapter, NetBuffer, Exempt)))
                {
                    Adapter->TxOk++;
                }
                else
                {
                    Adapter->TxError++;
                    Status = NDIS_STATUS_FAILURE;
                }
            }
        }

        RtwCompleteNbls(Adapter, Nbl, Status, FALSE);
        InterlockedDecrement(&Adapter->TxPending);
    }
}

static
VOID
NTAPI
RtwMiniportSendNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG SendFlags)
{
    PRTW_ADAPTER Adapter = MiniportAdapterContext;
    BOOLEAN AtDispatch = NDIS_TEST_SEND_AT_DISPATCH_LEVEL(SendFlags) ? TRUE : FALSE;
    PNET_BUFFER_LIST Tail;
    LONG Count = 1;

    UNREFERENCED_PARAMETER(PortNumber);

    if (!Adapter->DataPathRunning || Adapter->Halting)
    {
        RtwCompleteNbls(Adapter, NetBufferLists, NDIS_STATUS_PAUSED, AtDispatch);
        return;
    }

    if (Adapter->LinkState != RtwLinkAssociated)
    {
        RtwCompleteNbls(Adapter, NetBufferLists, NDIS_STATUS_MEDIA_DISCONNECTED, AtDispatch);
        return;
    }

    for (Tail = NetBufferLists; NET_BUFFER_LIST_NEXT_NBL(Tail); Tail = NET_BUFFER_LIST_NEXT_NBL(Tail))
        Count++;

    InterlockedExchangeAdd(&Adapter->TxPending, Count);
    NdisAcquireSpinLock(&Adapter->TxQueueLock);
    if (Adapter->TxQueueTail)
        NET_BUFFER_LIST_NEXT_NBL(Adapter->TxQueueTail) = NetBufferLists;
    else
        Adapter->TxQueueHead = NetBufferLists;
    Adapter->TxQueueTail = Tail;
    NdisReleaseSpinLock(&Adapter->TxQueueLock);

    KeSetEvent(&Adapter->WorkerEvent, IO_NETWORK_INCREMENT, FALSE);
}

static
VOID
NTAPI
RtwMiniportCancelSend(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID CancelId)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(CancelId);
}

static
VOID
RtwFlushTxQueue(
    _In_ PRTW_ADAPTER Adapter,
    _In_ NDIS_STATUS Status)
{
    PNET_BUFFER_LIST Nbl;

    while ((Nbl = RtwDequeueNbl(Adapter)) != NULL)
    {
        RtwCompleteNbls(Adapter, Nbl, Status, FALSE);
        InterlockedDecrement(&Adapter->TxPending);
    }
}

static
NDIS_STATUS
RtwSetAttributes(
    _In_ PRTW_ADAPTER Adapter)
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
    General.MtuSize = RTW_MTU_SIZE;
    General.MaxXmitLinkSpeed = RTW_LINK_SPEED_BPS;
    General.MaxRcvLinkSpeed = RTW_LINK_SPEED_BPS;
    General.XmitLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    General.RcvLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    General.MediaConnectState = MediaConnectStateDisconnected;
    General.MediaDuplexState = MediaDuplexStateFull;
    General.LookaheadSize = RTW_MAX_80211_FRAME;
    General.MacOptions = NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
                         NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
                         NDIS_MAC_OPTION_NO_LOOPBACK;
    General.SupportedPacketFilters = NDIS_PACKET_TYPE_DIRECTED |
                                     NDIS_PACKET_TYPE_MULTICAST |
                                     NDIS_PACKET_TYPE_ALL_MULTICAST |
                                     NDIS_PACKET_TYPE_BROADCAST;
    General.MaxMulticastListSize = 32;
    General.MacAddressLength = RTW_ADDRESS_LENGTH;
    RtlCopyMemory(General.PermanentMacAddress, Adapter->PermanentAddress, RTW_ADDRESS_LENGTH);
    RtlCopyMemory(General.CurrentMacAddress, Adapter->CurrentAddress, RTW_ADDRESS_LENGTH);
    General.AccessType = NET_IF_ACCESS_BROADCAST;
    General.DirectionType = NET_IF_DIRECTION_SENDRECEIVE;
    General.ConnectionType = NET_IF_CONNECTION_DEDICATED;
    General.IfType = IF_TYPE_IEEE80211;
    General.IfConnectorPresent = TRUE;
    General.SupportedOidList = RtwSupportedOids;
    General.SupportedOidListLength = RtwSupportedOidCount * sizeof(NDIS_OID);

    return NdisMSetMiniportAttributes(Adapter->MiniportAdapterHandle,
                                      (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&General);
}

static
VOID
RtwReleaseAdapter(
    _In_ PRTW_ADAPTER Adapter)
{
    if (Adapter->CommandThread)
    {
        Adapter->CommandStop = TRUE;
        RtwWakeCommand(Adapter);
        KeWaitForSingleObject(&Adapter->CommandStopped, Executive, KernelMode, FALSE, NULL);
        ObDereferenceObject(Adapter->CommandThread);
        Adapter->CommandThread = NULL;
    }

    Adapter->Halting = TRUE;
    Adapter->DataPathRunning = FALSE;
    RtwStopRx(Adapter);
    RtwFlushTxQueue(Adapter, NDIS_STATUS_FAILURE);
    if (Adapter->PowerOn)
        RtwPowerOff(Adapter);
    RtwSdioClose(Adapter);

    if (Adapter->RxNblPool)
        NdisFreeNetBufferListPool(Adapter->RxNblPool);

    RtwClearKeys(Adapter);
    RtwFree(Adapter->LogicalEfuse);
    if (Adapter->Firmware)
        ExFreePoolWithTag(Adapter->Firmware, RTW_TAG);
    NdisFreeSpinLock(&Adapter->Lock);
    NdisFreeSpinLock(&Adapter->TxQueueLock);
    RtwFree(Adapter);
}

static
NTSTATUS
RtwHardwareInitialize(
    _In_ PRTW_ADAPTER Adapter)
{
    NTSTATUS Status;

    Status = RtwSdioOpen(Adapter);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RTW8852BS: SDIO setup failed 0x%08lx\n", Status);
        return Status;
    }

    Status = RtwHardwareProbe(Adapter);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RTW8852BS: hardware probe failed 0x%08lx\n", Status);
        return Status;
    }

    Status = RtwLoadFirmware(Adapter);
    if (NT_SUCCESS(Status))
        Status = RtwDownloadFirmware(Adapter);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RTW8852BS: firmware start failed 0x%08lx\n", Status);
        return Status;
    }

    Status = RtwMacInit(Adapter);
    if (NT_SUCCESS(Status))
        Status = RtwStartRx(Adapter);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RTW8852BS: MAC initialization failed 0x%08lx\n", Status);
        return Status;
    }

    Status = RtwPhyInit(Adapter, RTW_INITIAL_CHANNEL);
    if (NT_SUCCESS(Status))
        Status = RtwVifInit(Adapter);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RTW8852BS: radio initialization failed 0x%08lx\n", Status);
        return Status;
    }

    RtwSetRxFilter(Adapter, TRUE);
    return STATUS_SUCCESS;
}

static
NDIS_STATUS
NTAPI
RtwMiniportInitializeEx(
    _In_ NDIS_HANDLE MiniportAdapterHandle,
    _In_ NDIS_HANDLE MiniportDriverContext,
    _In_ PNDIS_MINIPORT_INIT_PARAMETERS InitParameters)
{
    NET_BUFFER_LIST_POOL_PARAMETERS PoolParameters;
    PDEVICE_OBJECT FunctionalDeviceObject;
    HANDLE ThreadHandle;
    PRTW_ADAPTER Adapter;
    NDIS_STATUS Status;
    NTSTATUS NtStatus;

    UNREFERENCED_PARAMETER(MiniportDriverContext);
    UNREFERENCED_PARAMETER(InitParameters);

    Adapter = RtwAllocate(sizeof(*Adapter));
    if (!Adapter)
        return NDIS_STATUS_RESOURCES;

    Adapter->MiniportAdapterHandle = MiniportAdapterHandle;
    NdisAllocateSpinLock(&Adapter->Lock);
    NdisAllocateSpinLock(&Adapter->TxQueueLock);
    KeInitializeEvent(&Adapter->CommandEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Adapter->CommandStopped, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->MlmeEvent, NotificationEvent, FALSE);

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
    Adapter->LinkState = RtwLinkIdle;

    NdisMGetDeviceProperty(MiniportAdapterHandle,
                           &Adapter->PhysicalDeviceObject,
                           &FunctionalDeviceObject,
                           &Adapter->LowerDeviceObject,
                           NULL,
                           NULL);
    if (!Adapter->PhysicalDeviceObject)
    {
        Status = NDIS_STATUS_ADAPTER_NOT_FOUND;
        goto Fail;
    }

    NtStatus = RtwHardwareInitialize(Adapter);
    if (!NT_SUCCESS(NtStatus))
    {
        Status = NtStatus == STATUS_NOT_FOUND ? NDIS_STATUS_ADAPTER_NOT_FOUND : NDIS_STATUS_HARD_ERRORS;
        goto Fail;
    }

    RtlZeroMemory(&PoolParameters, sizeof(PoolParameters));
    PoolParameters.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    PoolParameters.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParameters.Header.Size = NDIS_SIZEOF_NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParameters.ProtocolId = NDIS_PROTOCOL_ID_DEFAULT;
    PoolParameters.fAllocateNetBuffer = TRUE;
    PoolParameters.PoolTag = RTW_TAG;
    Adapter->RxNblPool = NdisAllocateNetBufferListPool(MiniportAdapterHandle, &PoolParameters);
    if (!Adapter->RxNblPool)
    {
        Status = NDIS_STATUS_RESOURCES;
        goto Fail;
    }

    Status = RtwSetAttributes(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Fail;

    NtStatus = PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL, RtwCommandThread, Adapter);
    if (!NT_SUCCESS(NtStatus))
    {
        Status = NDIS_STATUS_RESOURCES;
        goto Fail;
    }

    ObReferenceObjectByHandle(ThreadHandle, THREAD_ALL_ACCESS, NULL, KernelMode, (PVOID*)&Adapter->CommandThread, NULL);
    ZwClose(ThreadHandle);

    Adapter->HardwareReady = TRUE;
    DPRINT1("RTW8852BS: adapter ready\n");
    return NDIS_STATUS_SUCCESS;

Fail:
    RtwReleaseAdapter(Adapter);
    return Status;
}

static
VOID
NTAPI
RtwMiniportHaltEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_HALT_ACTION HaltAction)
{
    PRTW_ADAPTER Adapter = MiniportAdapterContext;

    UNREFERENCED_PARAMETER(HaltAction);

    Adapter->DataPathRunning = FALSE;
    RtwReleaseAdapter(Adapter);
}

static
NDIS_STATUS
NTAPI
RtwMiniportPauseEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_PAUSE_PARAMETERS PauseParameters)
{
    PRTW_ADAPTER Adapter = MiniportAdapterContext;
    ULONG Wait;

    UNREFERENCED_PARAMETER(PauseParameters);

    Adapter->DataPathRunning = FALSE;
    KeSetEvent(&Adapter->WorkerEvent, IO_NETWORK_INCREMENT, FALSE);
    for (Wait = 0; Wait < 500 && (Adapter->RxIndicated || Adapter->TxPending); Wait++)
        RtwSleep(2);
    RtwFlushTxQueue(Adapter, NDIS_STATUS_PAUSED);

    return NDIS_STATUS_SUCCESS;
}

static
NDIS_STATUS
NTAPI
RtwMiniportRestartEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_RESTART_PARAMETERS RestartParameters)
{
    PRTW_ADAPTER Adapter = MiniportAdapterContext;

    UNREFERENCED_PARAMETER(RestartParameters);

    Adapter->DataPathRunning = TRUE;
    RtwIndicateLinkState(Adapter, Adapter->LinkState == RtwLinkAssociated);
    return NDIS_STATUS_SUCCESS;
}

static
VOID
NTAPI
RtwMiniportShutdownEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_SHUTDOWN_ACTION ShutdownAction)
{
    PRTW_ADAPTER Adapter = MiniportAdapterContext;

    UNREFERENCED_PARAMETER(ShutdownAction);

    Adapter->DataPathRunning = FALSE;
}

static
VOID
NTAPI
RtwMiniportDevicePnpEventNotify(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_DEVICE_PNP_EVENT NetDevicePnPEvent)
{
    PRTW_ADAPTER Adapter = MiniportAdapterContext;

    if (NetDevicePnPEvent->DevicePnPEvent == NdisDevicePnPEventSurpriseRemoved)
    {
        Adapter->Halting = TRUE;
        Adapter->DataPathRunning = FALSE;
    }
}

static
VOID
NTAPI
RtwMiniportUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    if (RtwMiniportDriverHandle)
    {
        NdisMDeregisterMiniportDriver(RtwMiniportDriverHandle);
        RtwMiniportDriverHandle = NULL;
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
    Characteristics.InitializeHandlerEx = RtwMiniportInitializeEx;
    Characteristics.HaltHandlerEx = RtwMiniportHaltEx;
    Characteristics.UnloadHandler = RtwMiniportUnload;
    Characteristics.PauseHandler = RtwMiniportPauseEx;
    Characteristics.RestartHandler = RtwMiniportRestartEx;
    Characteristics.OidRequestHandler = RtwMiniportOidRequest;
    Characteristics.SendNetBufferListsHandler = RtwMiniportSendNetBufferLists;
    Characteristics.ReturnNetBufferListsHandler = RtwMiniportReturnNetBufferLists;
    Characteristics.CancelSendHandler = RtwMiniportCancelSend;
    Characteristics.DevicePnPEventNotifyHandler = RtwMiniportDevicePnpEventNotify;
    Characteristics.ShutdownHandlerEx = RtwMiniportShutdownEx;
    Characteristics.CancelOidRequestHandler = RtwMiniportCancelOidRequest;

    return NdisMRegisterMiniportDriver(DriverObject, RegistryPath, NULL, &Characteristics, &RtwMiniportDriverHandle);
}
