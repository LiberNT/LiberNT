/*
 * PROJECT:     LiberNT NDIS library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NDIS 6 control device objects registered with NdisRegisterDeviceEx
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ndis6_internal.h"

#ifndef EXPORT
#define EXPORT NTAPI
#endif

/* ============================================================================
 *  E1: NdisRegisterDeviceEx / NdisDeregisterDeviceEx
 *
 *  Creates a separate control device object (DO) for miniports that expose
 *  an IOCTL interface unrelated to the adapter's datapath. Used by WFP
 *  callout drivers, WWAN control channels, and filter drivers that need
 *  their own user-mode interface.
 *
 *  The caller provides NDIS_DEVICE_OBJECT_ATTRIBUTES with device name,
 *  symbolic link name, and a PDRIVER_DISPATCH array for the major
 *  functions (Create/Close/Cleanup/DeviceControl). We call IoCreateDevice,
 *  install the dispatch routines on the driver object, and optionally
 *  create a symbolic link.
 * ============================================================================ */

typedef struct _NDIS6_CONTROL_DEVICE
{
    LIST_ENTRY          ListEntry;
    PDEVICE_OBJECT      DeviceObject;
    UNICODE_STRING      SymbolicName;
    BOOLEAN             SymbolicLinkCreated;
    /* The control driver's own major-function handlers. IRPs are demuxed by
     * target device (Ndis6ControlDemuxDispatch) so one driver's handlers never
     * run for another device sharing the driver object (e.g. the miniport FDO,
     * or another driver's control device). */
    PDRIVER_DISPATCH    MajorFunctions[IRP_MJ_MAXIMUM_FUNCTION + 1];
} NDIS6_CONTROL_DEVICE, *PNDIS6_CONTROL_DEVICE;

/* PNP/POWER/SYSTEM_CONTROL on a miniport driver object belong to NDIS, never to
 * a control driver's handlers. */
#define NDIS6_MJ_IS_NDIS_OWNED(mj) \
    ((mj) == IRP_MJ_PNP || (mj) == IRP_MJ_POWER || (mj) == IRP_MJ_SYSTEM_CONTROL)
#define NDIS6_CONTROL_NAME_TAG 'mSyN'

/* All live control devices, so NdisGetDeviceReservedExtension can tell a
 * NdisRegisterDeviceEx device object apart from a miniport FDO. */
static LIST_ENTRY g_Ndis6CtlDevList = { &g_Ndis6CtlDevList, &g_Ndis6CtlDevList };
static KSPIN_LOCK g_Ndis6CtlDevLock;

PDEVICE_OBJECT
Ndis6GetFilterOrControlIoWorkItemObject(
    _In_ NDIS_HANDLE NdisObjectHandle)
{
    PDEVICE_OBJECT Object = NULL;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    Ndis6FilterDriverListInit();
    KeAcquireSpinLock(&g_Ndis6FilterDriverListLock, &OldIrql);
    for (Entry = g_Ndis6FilterDriverList.Flink;
         Entry != &g_Ndis6FilterDriverList;
         Entry = Entry->Flink)
    {
        PNDIS6_FILTER_DRIVER_BLOCK Block =
            CONTAINING_RECORD(Entry, NDIS6_FILTER_DRIVER_BLOCK, ListEntry);

        if ((NDIS_HANDLE)Block == NdisObjectHandle)
        {
            Object = (PDEVICE_OBJECT)Block->DriverObject;
            break;
        }
    }
    KeReleaseSpinLock(&g_Ndis6FilterDriverListLock, OldIrql);

    if (Object != NULL)
        return Object;

    KeAcquireSpinLock(&g_Ndis6CtlDevLock, &OldIrql);
    for (Entry = g_Ndis6CtlDevList.Flink;
         Entry != &g_Ndis6CtlDevList;
         Entry = Entry->Flink)
    {
        PNDIS6_CONTROL_DEVICE CtlDev =
            CONTAINING_RECORD(Entry, NDIS6_CONTROL_DEVICE, ListEntry);

        if ((NDIS_HANDLE)CtlDev == NdisObjectHandle)
        {
            Object = CtlDev->DeviceObject;
            break;
        }
    }
    KeReleaseSpinLock(&g_Ndis6CtlDevLock, OldIrql);
    return Object;
}

/* TRUE if DeviceObject is a control device created by NdisRegisterDeviceEx.
 * The hybrid KMDF demux (60driver.c) uses this so a control device with no
 * handler for a major function is never misrouted into KMDF's dispatch. */
BOOLEAN
Ndis6DeviceIsControlDevice(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PLIST_ENTRY Entry;
    BOOLEAN     Found = FALSE;
    KIRQL       OldIrql;

    KeAcquireSpinLock(&g_Ndis6CtlDevLock, &OldIrql);
    for (Entry = g_Ndis6CtlDevList.Flink; Entry != &g_Ndis6CtlDevList; Entry = Entry->Flink)
    {
        PNDIS6_CONTROL_DEVICE CtlDev = CONTAINING_RECORD(Entry, NDIS6_CONTROL_DEVICE, ListEntry);
        if (CtlDev->DeviceObject == DeviceObject)
        {
            Found = TRUE;
            break;
        }
    }
    KeReleaseSpinLock(&g_Ndis6CtlDevLock, OldIrql);
    return Found;
}

/*
 * Ndis6ControlDemuxDispatch
 *
 * Single IRP entry point installed on a miniport driver object for the major
 * functions a control driver registered. It routes each IRP to the control
 * device that owns it, so a control driver's handler never runs for the
 * miniport FDO or for a different driver's control device that happens to share
 * the driver object. Unknown targets (miniport FDO opens) complete benignly.
 */
static NTSTATUS
NTAPI
Ndis6ControlDemuxDispatch(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION    IoStack = IoGetCurrentIrpStackLocation(Irp);
    PNDIS6_CONTROL_DEVICE Match = NULL;
    PLIST_ENTRY           Entry;
    KIRQL                 OldIrql;
    NTSTATUS              Status;

    KeAcquireSpinLock(&g_Ndis6CtlDevLock, &OldIrql);
    for (Entry = g_Ndis6CtlDevList.Flink;
         Entry != &g_Ndis6CtlDevList;
         Entry = Entry->Flink)
    {
        PNDIS6_CONTROL_DEVICE CtlDev =
            CONTAINING_RECORD(Entry, NDIS6_CONTROL_DEVICE, ListEntry);
        if (CtlDev->DeviceObject == DeviceObject)
        {
            Match = CtlDev;
            break;
        }
    }
    KeReleaseSpinLock(&g_Ndis6CtlDevLock, OldIrql);

    if (Match != NULL && Match->MajorFunctions[IoStack->MajorFunction] != NULL)
        return Match->MajorFunctions[IoStack->MajorFunction](DeviceObject, Irp);

    /* Hybrid KMDF+NDIS driver: NdisRegisterDeviceEx overrode dispatch slots
     * KMDF had installed, so IRPs for KMDF's own device objects land here.
     * Hand them back to the dispatch KMDF originally registered. */
    if (Match == NULL)
    {
        PDRIVER_DISPATCH Original = Ndis6HybridGetOriginalDispatch(DeviceObject, IoStack->MajorFunction);
        if (Original != NULL)
            return Original(DeviceObject, Irp);

        /* Miniport FDO: user-mode opens + the global-stats OID IOCTL. */
        if (Ndis6TryDispatchAdapterFdoIrp(DeviceObject, Irp, &Status))
            return Status;
    }

    /* Not a control device (e.g. a user-mode open of the miniport FDO): a bare
     * open/close succeeds, everything else is unsupported. */
    Status = (IoStack->MajorFunction == IRP_MJ_CREATE ||
              IoStack->MajorFunction == IRP_MJ_CLOSE ||
              IoStack->MajorFunction == IRP_MJ_CLEANUP)
                 ? STATUS_SUCCESS : STATUS_NOT_SUPPORTED;
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

NDIS_STATUS
NTAPI
NdisRegisterDeviceEx(
    _In_  NDIS_HANDLE   NdisHandle,
    _In_  PNDIS_DEVICE_OBJECT_ATTRIBUTES DeviceObjectAttributes,
    _Out_ PDEVICE_OBJECT* pDeviceObject,
    _Out_ PNDIS_HANDLE  NdisDeviceHandle)
{
    PNDIS_DEVICE_OBJECT_ATTRIBUTES Attrs = DeviceObjectAttributes;
    PNDIS6_CONTROL_DEVICE   CtlDev;
    PDRIVER_OBJECT          DriverObject = NULL;
    PDEVICE_OBJECT          Device       = NULL;
    NTSTATUS                Status;
    ULONG                   DeviceExtensionSize;
    ULONG                   i;
    KIRQL                   OldIrql;
    USHORT                  SymbolicMaximumLength;

    if (pDeviceObject != NULL)
        *pDeviceObject = NULL;
    if (NdisDeviceHandle != NULL)
        *NdisDeviceHandle = NULL;

    if (Attrs == NULL || pDeviceObject == NULL || NdisDeviceHandle == NULL ||
        Attrs->DeviceName == NULL || Attrs->MajorFunctions == NULL)
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    if (Attrs->ExtensionSize > MAXULONG - sizeof(*CtlDev))
        return NDIS_STATUS_INVALID_PARAMETER;
    DeviceExtensionSize = Attrs->ExtensionSize + sizeof(*CtlDev);

    if (Attrs->SymbolicName != NULL &&
        (Attrs->SymbolicName->Length > Attrs->SymbolicName->MaximumLength ||
         (Attrs->SymbolicName->Length & (sizeof(WCHAR) - 1)) != 0 ||
         Attrs->SymbolicName->Length > MAXUSHORT - sizeof(WCHAR) ||
         (Attrs->SymbolicName->Length != 0 &&
          Attrs->SymbolicName->Buffer == NULL)))
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    /* Resolve the caller's own driver object. For a miniport, NdisHandle is the
     * NDIS6_DRIVER_BLOCK returned by NdisMRegisterMiniportDriver; validate it
     * against the registered-driver list and use ITS driver object. Using the
     * wrong driver object cross-installs a control driver's handlers over an
     * unrelated device (e.g. another driver's \Device\Nwifi), which then faults
     * inside that driver's handler. */
    KeAcquireSpinLock(&g_Ndis6DriverListLock, &OldIrql);
    for (PLIST_ENTRY Entry = g_Ndis6DriverList.Flink;
         Entry != &g_Ndis6DriverList;
         Entry = Entry->Flink)
    {
        PNDIS6_DRIVER_BLOCK Block =
            CONTAINING_RECORD(Entry, NDIS6_DRIVER_BLOCK, ListEntry);
        if ((NDIS_HANDLE)Block == NdisHandle)
        {
            DriverObject = Block->DriverObject;
            break;
        }
    }
    KeReleaseSpinLock(&g_Ndis6DriverListLock, OldIrql);

    if (DriverObject == NULL)
    {
        Ndis6FilterDriverListInit();
        KeAcquireSpinLock(&g_Ndis6FilterDriverListLock, &OldIrql);
        for (PLIST_ENTRY Entry = g_Ndis6FilterDriverList.Flink;
             Entry != &g_Ndis6FilterDriverList;
             Entry = Entry->Flink)
        {
            PNDIS6_FILTER_DRIVER_BLOCK Block =
                CONTAINING_RECORD(Entry, NDIS6_FILTER_DRIVER_BLOCK, ListEntry);
            if ((NDIS_HANDLE)Block == NdisHandle)
            {
                DriverObject = Block->DriverObject;
                break;
            }
        }
        KeReleaseSpinLock(&g_Ndis6FilterDriverListLock, OldIrql);
    }

    if (DriverObject == NULL)
        return NDIS_STATUS_NOT_SUPPORTED;

    /* Keep the NDIS control block in the device extension so its lifetime is
     * protected by the I/O manager while an IRP dispatch is still active. */
    Status = IoCreateDevice(DriverObject,
                            DeviceExtensionSize,
                            Attrs->DeviceName,
                            FILE_DEVICE_NETWORK,
                            0,
                            FALSE,
                            &Device);
    if (!NT_SUCCESS(Status))
        return (NDIS_STATUS)Status;

    CtlDev = (PNDIS6_CONTROL_DEVICE)Device->DeviceExtension;
    RtlZeroMemory(CtlDev, sizeof(*CtlDev));
    CtlDev->DeviceObject = Device;

    if (Attrs->SymbolicName != NULL)
    {
        SymbolicMaximumLength = Attrs->SymbolicName->Length + sizeof(WCHAR);
        CtlDev->SymbolicName.Buffer = ExAllocatePoolWithTag(NonPagedPool, SymbolicMaximumLength, NDIS6_CONTROL_NAME_TAG);
        if (CtlDev->SymbolicName.Buffer == NULL)
        {
            IoDeleteDevice(Device);
            return NDIS_STATUS_RESOURCES;
        }

        CtlDev->SymbolicName.Length = Attrs->SymbolicName->Length;
        CtlDev->SymbolicName.MaximumLength = SymbolicMaximumLength;
        if (Attrs->SymbolicName->Length != 0)
            RtlCopyMemory(CtlDev->SymbolicName.Buffer, Attrs->SymbolicName->Buffer, Attrs->SymbolicName->Length);
        CtlDev->SymbolicName.Buffer[Attrs->SymbolicName->Length / sizeof(WCHAR)] = UNICODE_NULL;
    }

    /* Keep the caller's handlers on the control device, and route the driver
     * object's matching slots through the per-device demux (never the raw
     * handler, so the miniport FDO and other devices are not affected). NDIS
     * keeps PNP/POWER/SYSTEM_CONTROL. */
    for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++)
    {
        if (Attrs->MajorFunctions[i] != NULL && !NDIS6_MJ_IS_NDIS_OWNED(i))
        {
            CtlDev->MajorFunctions[i]      = Attrs->MajorFunctions[i];
            DriverObject->MajorFunction[i] = Ndis6ControlDemuxDispatch;
        }
    }

    /* Symbolic link so user-mode can CreateFile on it. */
    if (Attrs->SymbolicName != NULL)
    {
        Status = IoCreateSymbolicLink(&CtlDev->SymbolicName,
                                      Attrs->DeviceName);
        if (NT_SUCCESS(Status))
        {
            CtlDev->SymbolicLinkCreated  = TRUE;
        }
        else
        {
            /* Symbolic link failure is non-fatal. */
            ExFreePoolWithTag(CtlDev->SymbolicName.Buffer, NDIS6_CONTROL_NAME_TAG);
            RtlZeroMemory(&CtlDev->SymbolicName, sizeof(CtlDev->SymbolicName));
        }
    }

    ExInterlockedInsertTailList(&g_Ndis6CtlDevList, &CtlDev->ListEntry,
                                &g_Ndis6CtlDevLock);
    Device->Flags &= ~DO_DEVICE_INITIALIZING;

    *pDeviceObject    = Device;
    *NdisDeviceHandle = (NDIS_HANDLE)CtlDev;
    return NDIS_STATUS_SUCCESS;
}

VOID
NTAPI
NdisDeregisterDeviceEx(
    _In_ NDIS_HANDLE NdisDeviceHandle)
{
    PNDIS6_CONTROL_DEVICE CtlDev = (PNDIS6_CONTROL_DEVICE)NdisDeviceHandle;
    KIRQL OldIrql;

    if (CtlDev == NULL)
        return;

    KeAcquireSpinLock(&g_Ndis6CtlDevLock, &OldIrql);
    RemoveEntryList(&CtlDev->ListEntry);
    KeReleaseSpinLock(&g_Ndis6CtlDevLock, OldIrql);

    if (CtlDev->SymbolicLinkCreated)
        IoDeleteSymbolicLink(&CtlDev->SymbolicName);
    if (CtlDev->SymbolicName.Buffer != NULL)
        ExFreePoolWithTag(CtlDev->SymbolicName.Buffer, NDIS6_CONTROL_NAME_TAG);
    IoDeleteDevice(CtlDev->DeviceObject);
}

/*
 * NdisGetDeviceReservedExtension
 *
 * For a device created by NdisRegisterDeviceEx the NDIS control block leads
 * the device extension and the caller's reserved area follows it. For a
 * miniport FDO the reserved area is the WdfReserved scratch space in
 * LOGICAL_ADAPTER, where NDIS-WDF miniports keep their framework context.
 */
PVOID
NTAPI
NdisGetDeviceReservedExtension(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PLOGICAL_ADAPTER Adapter;
    PLIST_ENTRY Entry;
    PNDIS6_CONTROL_DEVICE CtlDev = NULL;
    KIRQL OldIrql;
    BOOLEAN IsCtlDev = FALSE;

    if (DeviceObject == NULL)
        return NULL;

    KeAcquireSpinLock(&g_Ndis6CtlDevLock, &OldIrql);
    for (Entry = g_Ndis6CtlDevList.Flink;
         Entry != &g_Ndis6CtlDevList;
         Entry = Entry->Flink)
    {
        CtlDev = CONTAINING_RECORD(Entry,
                                   NDIS6_CONTROL_DEVICE,
                                   ListEntry);
        if (CtlDev->DeviceObject == DeviceObject)
        {
            IsCtlDev = TRUE;
            break;
        }
    }
    KeReleaseSpinLock(&g_Ndis6CtlDevLock, OldIrql);

    if (IsCtlDev)
        return (PVOID)(CtlDev + 1);

    /* WDF-managed NetAdapterCx FDOs do not store the logical adapter in
     * DeviceExtension, so resolve both native and WDF-owned FDOs through
     * the registered adapter list. */
    Adapter = Ndis6FindAdapterByFdo(DeviceObject);
    if (Adapter != NULL)
        return &Adapter->WdfReserved[0];

    return NULL;
}

/* EOF */
