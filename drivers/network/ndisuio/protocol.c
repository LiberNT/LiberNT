/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS NDIS User I/O driver
 * FILE:        protocol.c
 * PURPOSE:     Protocol stuff
 * PROGRAMMERS: Cameron Gutman (cameron.gutman@reactos.org)
 */

#include "ndisuio.h"

#define NDEBUG
#include <debug.h>

VOID
NTAPI
NduOpenAdapterComplete(NDIS_HANDLE ProtocolBindingContext,
                       NDIS_STATUS Status)
{
    PNDISUIO_ADAPTER_CONTEXT AdapterContext = ProtocolBindingContext;

    DPRINT("Asynchronous adapter open completed\n");

    /* Store the final status and signal the event */
    AdapterContext->AsyncStatus = Status;
    KeSetEvent(&AdapterContext->AsyncEvent, IO_NO_INCREMENT, FALSE);
}

VOID
NTAPI
NduCloseAdapterComplete(NDIS_HANDLE ProtocolBindingContext)
{
    PNDISUIO_ADAPTER_CONTEXT AdapterContext = ProtocolBindingContext;

    DPRINT("Asynchronous adapter close completed\n");

    /* Store the final status and signal the event */
    AdapterContext->AsyncStatus = NDIS_STATUS_SUCCESS;
    KeSetEvent(&AdapterContext->AsyncEvent, IO_NO_INCREMENT, FALSE);
}

NDIS_STATUS
NTAPI
NduNetPnPEvent(NDIS_HANDLE ProtocolBindingContext,
               PNET_PNP_EVENT_NOTIFICATION NetPnPEventNotification)
{
    PNDISUIO_ADAPTER_CONTEXT AdapterContext = ProtocolBindingContext;
    PNET_PNP_EVENT NetPnPEvent = &NetPnPEventNotification->NetPnPEvent;
    PNDIS_DEVICE_POWER_STATE PowerState;
    BOOLEAN WaitForSends;
    KIRQL OldIrql;

    DPRINT("NetPnPEvent\n");

    switch (NetPnPEvent->NetEvent)
    {
        case NetEventPause:
            DPRINT("NetPnPEvent: Pause\n");

            KeAcquireSpinLock(&AdapterContext->Spinlock, &OldIrql);
            AdapterContext->Paused = TRUE;
            KeClearEvent(&AdapterContext->SendsDoneEvent);
            WaitForSends = (AdapterContext->PendingSends != 0);
            KeReleaseSpinLock(&AdapterContext->Spinlock, OldIrql);

            if (WaitForSends)
            {
                KeWaitForSingleObject(&AdapterContext->SendsDoneEvent,
                                      Executive,
                                      KernelMode,
                                      FALSE,
                                      NULL);
            }
            return NDIS_STATUS_SUCCESS;

        case NetEventRestart:
            DPRINT("NetPnPEvent: Restart\n");

            KeAcquireSpinLock(&AdapterContext->Spinlock, &OldIrql);
            AdapterContext->Paused = FALSE;
            KeReleaseSpinLock(&AdapterContext->Spinlock, OldIrql);
            return NDIS_STATUS_SUCCESS;

        case NetEventQueryRemoveDevice:
            /* Nothing to do */
            DPRINT1("NetPnPEvent: QueryRemoveDevice\n");
            return NDIS_STATUS_SUCCESS;

        case NetEventSetPower:
            ASSERT(NetPnPEvent->BufferLength >= sizeof(*PowerState));

            PowerState = NetPnPEvent->Buffer;
            switch (*PowerState)
            {
                case NdisDeviceStateD0:
                    DPRINT1("NetPnPEvent: SetPower D0\n");
                    return NDIS_STATUS_SUCCESS;

                default:
                    DPRINT1("NetPnPEvent: SetPower state %d not supported\n", *PowerState);
                    return NDIS_STATUS_FAILURE;
            }

        case NetEventQueryPower:
            DPRINT1("NetPnPEvent: QueryPower\n");
            return NDIS_STATUS_SUCCESS;

        case NetEventCancelRemoveDevice:
            DPRINT1("NetPnPEvent: CancelRemoveDevice\n");
            return NDIS_STATUS_SUCCESS;

        case NetEventReconfigure:
            DPRINT1("NetPnPEvent: Reconfigure\n");
            return NDIS_STATUS_SUCCESS;

        case NetEventBindList:
            DPRINT1("NetPnPEvent: BindList\n");
            return NDIS_STATUS_SUCCESS;

        case NetEventBindsComplete:
            DPRINT("NetPnPEvent: BindsComplete\n");
            return NDIS_STATUS_SUCCESS;

        case NetEventPnPCapabilities:
            DPRINT1("NetPnPEvent: PnPCapabilities\n");
            return NDIS_STATUS_SUCCESS;

        default:
            DPRINT1("NetPnPEvent unimplemented for net event 0x%x\n", NetPnPEvent->NetEvent);
            return NDIS_STATUS_FAILURE;
    }
}

VOID
NTAPI
NduSendComplete(NDIS_HANDLE ProtocolBindingContext,
                PNET_BUFFER_LIST NetBufferLists,
                ULONG SendCompleteFlags)
{
    PNET_BUFFER_LIST Nbl, NextNbl;

    DPRINT("Asynchronous adapter send completed\n");

    for (Nbl = NetBufferLists; Nbl != NULL; Nbl = NextNbl)
    {
        NextNbl = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;

        KeSetEvent((PKEVENT)Nbl->ProtocolReserved[0], IO_NO_INCREMENT, FALSE);
    }
}

VOID
NTAPI
NduRequestComplete(NDIS_HANDLE ProtocolBindingContext,
                   PNDIS_OID_REQUEST NdisRequest,
                   NDIS_STATUS Status)
{
    PNDISUIO_REQUEST Request = CONTAINING_RECORD(NdisRequest, NDISUIO_REQUEST, Request);

    DPRINT("Asynchronous adapter request completed\n");

    /* Store the final status and signal the event */
    Request->Status = Status;
    KeSetEvent(&Request->Event, IO_NO_INCREMENT, FALSE);
}

VOID
NTAPI
NduReceive(NDIS_HANDLE ProtocolBindingContext,
           PNET_BUFFER_LIST NetBufferLists,
           NDIS_PORT_NUMBER PortNumber,
           ULONG NumberOfNetBufferLists,
           ULONG ReceiveFlags)
{
    PNDISUIO_ADAPTER_CONTEXT AdapterContext = ProtocolBindingContext;
    PNDISUIO_PACKET_ENTRY PacketEntry;
    PNET_BUFFER_LIST Nbl;
    PNET_BUFFER NetBuffer;
    ULONG PacketSize;
    PVOID PacketData;
    BOOLEAN Queued = FALSE;

    for (Nbl = NetBufferLists; Nbl != NULL; Nbl = NET_BUFFER_LIST_NEXT_NBL(Nbl))
    {
        for (NetBuffer = NET_BUFFER_LIST_FIRST_NB(Nbl);
             NetBuffer != NULL;
             NetBuffer = NET_BUFFER_NEXT_NB(NetBuffer))
        {
            PacketSize = NET_BUFFER_DATA_LENGTH(NetBuffer);

            DPRINT("Received a %d byte packet\n", PacketSize);

            if (AdapterContext->OpenCount == 0 || PacketSize == 0)
                continue;

            PacketEntry = ExAllocatePool(NonPagedPool, sizeof(NDISUIO_PACKET_ENTRY) + PacketSize - 1);
            if (!PacketEntry)
                continue;

            PacketData = NdisGetDataBuffer(NetBuffer, PacketSize, PacketEntry->PacketData, 1, 0);
            if (!PacketData)
            {
                ExFreePool(PacketEntry);
                continue;
            }

            PacketEntry->PacketLength = PacketSize;
            if (PacketData != PacketEntry->PacketData)
                RtlCopyMemory(PacketEntry->PacketData, PacketData, PacketSize);

            ExInterlockedInsertTailList(&AdapterContext->PacketList,
                                        &PacketEntry->ListEntry,
                                        &AdapterContext->Spinlock);
            Queued = TRUE;
        }
    }

    if (Queued)
    {
        KeSetEvent(&AdapterContext->PacketReadEvent,
                   IO_NETWORK_INCREMENT,
                   FALSE);
    }

    if (NDIS_TEST_RECEIVE_CAN_PEND(ReceiveFlags))
    {
        NdisReturnNetBufferLists(AdapterContext->BindingHandle,
                                 NetBufferLists,
                                 NDIS_TEST_RECEIVE_AT_DISPATCH_LEVEL(ReceiveFlags) ?
                                 NDIS_RETURN_FLAGS_DISPATCH_LEVEL : 0);
    }
}

VOID
NTAPI
NduStatus(NDIS_HANDLE ProtocolBindingContext,
          PNDIS_STATUS_INDICATION StatusIndication)
{
    /* FIXME: Implement status tracking */
}

static
NDIS_STATUS
UnbindAdapterByContext(PNDISUIO_ADAPTER_CONTEXT AdapterContext)
{
    KIRQL OldIrql;
    PLIST_ENTRY CurrentEntry;
    PNDISUIO_OPEN_ENTRY OpenEntry;
    PNDISUIO_PACKET_ENTRY PacketEntry;
    NDIS_STATUS Status;

    DPRINT("Unbinding adapter %wZ\n", &AdapterContext->DeviceName);

    /* FIXME: We don't do anything with outstanding reads */

    /* Remove the adapter context from the global list */
    KeAcquireSpinLock(&GlobalAdapterListLock, &OldIrql);
    RemoveEntryList(&AdapterContext->ListEntry);
    KeReleaseSpinLock(&GlobalAdapterListLock, OldIrql);

    /* Free the device name string */
    RtlFreeUnicodeString(&AdapterContext->DeviceName);

    /* Invalidate all handles to this adapter */
    CurrentEntry = AdapterContext->OpenEntryList.Flink;
    while (CurrentEntry != &AdapterContext->OpenEntryList)
    {
        OpenEntry = CONTAINING_RECORD(CurrentEntry, NDISUIO_OPEN_ENTRY, ListEntry);

        /* Make sure the entry is sane */
        ASSERT(OpenEntry->FileObject);

        /* Remove the adapter context pointer */
        ASSERT(AdapterContext == OpenEntry->FileObject->FsContext);
        OpenEntry->FileObject->FsContext = NULL;
        AdapterContext->OpenCount--;

        /* Remove the open entry pointer */
        ASSERT(OpenEntry == OpenEntry->FileObject->FsContext2);
        OpenEntry->FileObject->FsContext2 = NULL;

        /* Move to the next entry */
        CurrentEntry = CurrentEntry->Flink;

        /* Free the open entry */
        ExFreePool(OpenEntry);
    }

    /* If this fails, we have a refcount mismatch somewhere */
    ASSERT(AdapterContext->OpenCount == 0);

    /* Free all pending packet entries */
    CurrentEntry = AdapterContext->PacketList.Flink;
    while (CurrentEntry != &AdapterContext->PacketList)
    {
        PacketEntry = CONTAINING_RECORD(CurrentEntry, NDISUIO_PACKET_ENTRY, ListEntry);

        /* Move to the next entry */
        CurrentEntry = CurrentEntry->Flink;

        /* Free the packet entry */
        ExFreePool(PacketEntry);
    }

    /* Send the close request */
    Status = NdisCloseAdapterEx(AdapterContext->BindingHandle);

    /* Wait for a pending close */
    if (Status == NDIS_STATUS_PENDING)
    {
        KeWaitForSingleObject(&AdapterContext->AsyncEvent,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
        Status = AdapterContext->AsyncStatus;
    }

    NdisFreeNetBufferListPool(AdapterContext->NblPoolHandle);

    /* Free the context */
    ExFreePool(AdapterContext);

    return Status;
}

static
NDIS_STATUS
BindAdapterByName(NDIS_HANDLE BindContext,
                  PNDIS_STRING DeviceName)
{
    PNDISUIO_ADAPTER_CONTEXT AdapterContext;
    NDIS_MEDIUM SupportedMedia[1] = {NdisMedium802_3};
    UINT SelectedMedium;
    NDIS_STATUS Status;
    NDIS_OPEN_PARAMETERS OpenParameters;
    NET_BUFFER_LIST_POOL_PARAMETERS PoolParameters;

    /* Allocate the adapter context */
    AdapterContext = ExAllocatePool(NonPagedPool, sizeof(*AdapterContext));
    if (!AdapterContext)
    {
        return NDIS_STATUS_RESOURCES;
    }

    /* Set up the adapter context */
    RtlZeroMemory(AdapterContext, sizeof(*AdapterContext));
    KeInitializeEvent(&AdapterContext->AsyncEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&AdapterContext->PacketReadEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&AdapterContext->SendsDoneEvent, NotificationEvent, FALSE);
    KeInitializeSpinLock(&AdapterContext->Spinlock);
    InitializeListHead(&AdapterContext->PacketList);
    InitializeListHead(&AdapterContext->OpenEntryList);
    AdapterContext->OpenCount = 0;
    AdapterContext->Paused = TRUE;

    AdapterContext->DeviceName.Length =
    AdapterContext->DeviceName.MaximumLength = DeviceName->Length;
    AdapterContext->DeviceName.Buffer = ExAllocatePool(NonPagedPool, DeviceName->Length);
    if (!AdapterContext->DeviceName.Buffer)
    {
        ExFreePool(AdapterContext);
        return NDIS_STATUS_RESOURCES;
    }

    /* Copy the device name into the adapter context */
    RtlCopyMemory(AdapterContext->DeviceName.Buffer, DeviceName->Buffer, DeviceName->Length);

    DPRINT("Binding adapter %wZ\n", &AdapterContext->DeviceName);

    RtlZeroMemory(&PoolParameters, sizeof(PoolParameters));
    PoolParameters.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    PoolParameters.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParameters.Header.Size = NDIS_SIZEOF_NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParameters.ProtocolId = NDIS_PROTOCOL_ID_DEFAULT;
    PoolParameters.fAllocateNetBuffer = TRUE;
    PoolParameters.PoolTag = 'oiuN';

    AdapterContext->NblPoolHandle = NdisAllocateNetBufferListPool(GlobalProtocolHandle,
                                                                  &PoolParameters);
    if (!AdapterContext->NblPoolHandle)
    {
        DPRINT1("Failed to allocate NBL pool\n");
        RtlFreeUnicodeString(&AdapterContext->DeviceName);
        ExFreePool(AdapterContext);
        return NDIS_STATUS_RESOURCES;
    }

    /* Send the open request */
    RtlZeroMemory(&OpenParameters, sizeof(OpenParameters));
    OpenParameters.Header.Type = NDIS_OBJECT_TYPE_OPEN_PARAMETERS;
    OpenParameters.Header.Revision = NDIS_OPEN_PARAMETERS_REVISION_1;
    OpenParameters.Header.Size = NDIS_SIZEOF_OPEN_PARAMETERS_REVISION_1;
    OpenParameters.AdapterName = DeviceName;
    OpenParameters.MediumArray = SupportedMedia;
    OpenParameters.MediumArraySize = 1;
    OpenParameters.SelectedMediumIndex = &SelectedMedium;

    Status = NdisOpenAdapterEx(GlobalProtocolHandle,
                               AdapterContext,
                               &OpenParameters,
                               BindContext,
                               &AdapterContext->BindingHandle);

    /* Wait for a pending open */
    if (Status == NDIS_STATUS_PENDING)
    {
        KeWaitForSingleObject(&AdapterContext->AsyncEvent,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
        Status = AdapterContext->AsyncStatus;
    }

    /* Check the final status */
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("Failed to open adapter for bind with status 0x%x\n", Status);
        NdisFreeNetBufferListPool(AdapterContext->NblPoolHandle);
        RtlFreeUnicodeString(&AdapterContext->DeviceName);
        ExFreePool(AdapterContext);
        return Status;
    }

    /* Add the adapter context to the global list */
    ExInterlockedInsertTailList(&GlobalAdapterList,
                                &AdapterContext->ListEntry,
                                &GlobalAdapterListLock);

    return STATUS_SUCCESS;
}

NDIS_STATUS
NTAPI
NduBindAdapter(NDIS_HANDLE ProtocolDriverContext,
               NDIS_HANDLE BindContext,
               PNDIS_BIND_PARAMETERS BindParameters)
{
    if (BindParameters->MediaType != NdisMedium802_3)
        return NDIS_STATUS_UNSUPPORTED_MEDIA;

    /* Use our helper function to create a context for this adapter */
    return BindAdapterByName(BindContext, BindParameters->AdapterName);
}

NDIS_STATUS
NTAPI
NduUnbindAdapter(NDIS_HANDLE UnbindContext,
                 NDIS_HANDLE ProtocolBindingContext)
{
    /* This is forced unbind. UnbindAdapterByContext() will take care of
     * invalidating file handles pointer to this adapter for us */
    return UnbindAdapterByContext(ProtocolBindingContext);
}
