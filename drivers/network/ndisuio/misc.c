/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS NDIS User I/O driver
 * FILE:        misc.c
 * PURPOSE:     Helper functions
 * PROGRAMMERS: Cameron Gutman (cameron.gutman@reactos.org)
 */

#include "ndisuio.h"

#define NDEBUG
#include <debug.h>

NDIS_STATUS
NduDoRequest(PNDISUIO_ADAPTER_CONTEXT AdapterContext,
             PNDISUIO_REQUEST Request)
{
    NDIS_STATUS Status;

    Request->Request.Header.Type = NDIS_OBJECT_TYPE_OID_REQUEST;
    Request->Request.Header.Revision = NDIS_OID_REQUEST_REVISION_1;
    Request->Request.Header.Size = NDIS_SIZEOF_OID_REQUEST_REVISION_1;
    KeInitializeEvent(&Request->Event, NotificationEvent, FALSE);

    Status = NdisOidRequest(AdapterContext->BindingHandle, &Request->Request);
    if (Status == NDIS_STATUS_PENDING)
    {
        KeWaitForSingleObject(&Request->Event,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
        Status = Request->Status;
    }

    return Status;
}

PNDISUIO_ADAPTER_CONTEXT
FindAdapterContextByName(PNDIS_STRING DeviceName)
{
    KIRQL OldIrql;
    PLIST_ENTRY CurrentEntry;
    PNDISUIO_ADAPTER_CONTEXT AdapterContext;

    KeAcquireSpinLock(&GlobalAdapterListLock, &OldIrql);
    CurrentEntry = GlobalAdapterList.Flink;
    while (CurrentEntry != &GlobalAdapterList)
    {
        AdapterContext = CONTAINING_RECORD(CurrentEntry, NDISUIO_ADAPTER_CONTEXT, ListEntry);

        /* Check if the device name matches */
        if (RtlEqualUnicodeString(&AdapterContext->DeviceName, DeviceName, TRUE))
        {
            KeReleaseSpinLock(&GlobalAdapterListLock, OldIrql);
            return AdapterContext;
        }

        CurrentEntry = CurrentEntry->Flink;
    }
    KeReleaseSpinLock(&GlobalAdapterListLock, OldIrql);

    return NULL;
}

VOID
ReferenceAdapterContext(PNDISUIO_ADAPTER_CONTEXT AdapterContext)
{
    /* Increment the open count */
    AdapterContext->OpenCount++;
}

VOID
DereferenceAdapterContextWithOpenEntry(PNDISUIO_ADAPTER_CONTEXT AdapterContext,
                                       PNDISUIO_OPEN_ENTRY OpenEntry)
{
    KIRQL OldIrql;

    /* Lock the adapter context */
    KeAcquireSpinLock(&AdapterContext->Spinlock, &OldIrql);

    /* Decrement the open count */
    AdapterContext->OpenCount--;

    /* Cleanup the open entry if we were given one */
    if (OpenEntry != NULL)
    {
        /* Remove the open entry */
        RemoveEntryList(&OpenEntry->ListEntry);

        /* Invalidate the FO */
        OpenEntry->FileObject->FsContext = NULL;
        OpenEntry->FileObject->FsContext2 = NULL;

        /* Free the open entry */
        ExFreePool(OpenEntry);
    }

    /* Release the adapter context lock */
    KeReleaseSpinLock(&AdapterContext->Spinlock, OldIrql);
}
