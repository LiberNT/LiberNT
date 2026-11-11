/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The questions the PnP manager puts to a device stack
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#include <wdmguid.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

NTSTATUS
NTAPI
IopInvalidDeviceRequest(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION StackPtr = IoGetCurrentIrpStackLocation(Irp);

    UNREFERENCED_PARAMETER(DeviceObject);

    if (StackPtr->MajorFunction == IRP_MJ_POWER)
    {
        Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
        PoStartNextPowerIrp(Irp);
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_NOT_SUPPORTED;
    }

    if (StackPtr->MajorFunction == IRP_MJ_PNP)
    {
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Irp->IoStatus.Status;
    }

    Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_INVALID_DEVICE_REQUEST;
}

static
NTSTATUS
NTAPI
IopSynchronousCallComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS
IopSynchronousCall(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIO_STACK_LOCATION IoStackLocation,
    OUT PVOID *Information)
{
    PIO_STACK_LOCATION StackPtr;
    IO_STATUS_BLOCK IoStatusBlock;
    PDEVICE_OBJECT TopDevice;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    PAGED_CODE();

    /* Plug and Play requests are dispatched at PASSIVE_LEVEL */
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    TopDevice = IoGetAttachedDeviceReference(DeviceObject);

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp = IoAllocateIrp(TopDevice->StackSize, FALSE);
    if (Irp == NULL)
    {
        ObDereferenceObject(TopDevice);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Irp->IoStatus.Information = 0;
    if (IoStackLocation->MinorFunction == IRP_MN_FILTER_RESOURCE_REQUIREMENTS)
    {
        Irp->IoStatus.Information =
            (ULONG_PTR)IoStackLocation->Parameters.FilterResourceRequirements.IoResourceRequirementList;
    }
    Irp->UserIosb = &IoStatusBlock;
    Irp->Flags = IRP_SYNCHRONOUS_API;
    Irp->RequestorMode = KernelMode;
    Irp->Tail.Overlay.Thread = PsGetCurrentThread();

    StackPtr = IoGetNextIrpStackLocation(Irp);

    RtlCopyMemory(StackPtr, IoStackLocation, sizeof(IO_STACK_LOCATION));

    StackPtr->MajorFunction = IRP_MJ_PNP;
    StackPtr->DeviceObject = TopDevice;

    /*
     * The packet is finished by this routine rather than by a completion APC,
     * which a caller with APCs held off would only get after returning.
     */
    IoSetCompletionRoutine(Irp, IopSynchronousCallComplete, &Event, TRUE, TRUE, TRUE);

    Status = IoCallDriver(TopDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    }
    else if (KeReadStateEvent(&Event) == 0)
    {
        /* The packet still belongs to a driver that answered without completing it */
        DPRINT1("PnP: %wZ answered minor %x with %lx without completing the request\n",
                &TopDevice->DriverObject->DriverName,
                IoStackLocation->MinorFunction,
                Status);
        ASSERT(KeReadStateEvent(&Event) != 0);

        if (Information != NULL) *Information = NULL;
        ObDereferenceObject(TopDevice);
        return Status;
    }

    Status = Irp->IoStatus.Status;
    if (Information != NULL)
    {
        *Information = (PVOID)Irp->IoStatus.Information;
    }

    IoFreeIrp(Irp);
    ObDereferenceObject(TopDevice);

    return Status;
}

NTSTATUS
NTAPI
IopInitiatePnpIrp(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIO_STATUS_BLOCK IoStatusBlock,
    IN UCHAR MinorFunction,
    IN PIO_STACK_LOCATION Stack)
{
    IO_STACK_LOCATION StackLocation;
    PVOID Information = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    RtlZeroMemory(&StackLocation, sizeof(StackLocation));

    if (Stack != NULL)
    {
        RtlCopyMemory(&StackLocation.Parameters,
                      &Stack->Parameters,
                      sizeof(StackLocation.Parameters));
    }

    StackLocation.MajorFunction = IRP_MJ_PNP;
    StackLocation.MinorFunction = MinorFunction;

    Status = IopSynchronousCall(DeviceObject, &StackLocation, &Information);

    if (IoStatusBlock != NULL)
    {
        IoStatusBlock->Status = Status;
        IoStatusBlock->Information = (ULONG_PTR)Information;
    }

    return Status;
}

/* PNP IRPS *******************************************************************/

NTSTATUS
PiIrpStartDevice(
    _In_ PDEVICE_NODE DeviceNode)
{
    IO_STACK_LOCATION StackLocation;
    NTSTATUS Status;

    PAGED_CODE();

    ASSERT(DeviceNode->State == DeviceNodeStartPending ||
           DeviceNode->State == DeviceNodeStarted);

    RtlZeroMemory(&StackLocation, sizeof(StackLocation));

    StackLocation.MajorFunction = IRP_MJ_PNP;
    StackLocation.MinorFunction = IRP_MN_START_DEVICE;
    StackLocation.Parameters.StartDevice.AllocatedResources =
        DeviceNode->ResourceList;
    StackLocation.Parameters.StartDevice.AllocatedResourcesTranslated =
        DeviceNode->ResourceListTranslated;

    Status = IopSynchronousCall(DeviceNode->PhysicalDeviceObject,
                                &StackLocation,
                                NULL);

    return Status;
}

NTSTATUS
PiIrpStopDevice(
    _In_ PDEVICE_NODE DeviceNode)
{
    IO_STACK_LOCATION StackLocation;

    PAGED_CODE();

    RtlZeroMemory(&StackLocation, sizeof(StackLocation));

    StackLocation.MajorFunction = IRP_MJ_PNP;
    StackLocation.MinorFunction = IRP_MN_STOP_DEVICE;

    return IopSynchronousCall(DeviceNode->PhysicalDeviceObject,
                              &StackLocation,
                              NULL);
}

NTSTATUS
PiIrpQueryStopDevice(
    _In_ PDEVICE_NODE DeviceNode)
{
    IO_STACK_LOCATION StackLocation;

    PAGED_CODE();

    RtlZeroMemory(&StackLocation, sizeof(StackLocation));

    StackLocation.MajorFunction = IRP_MJ_PNP;
    StackLocation.MinorFunction = IRP_MN_QUERY_STOP_DEVICE;

    return IopSynchronousCall(DeviceNode->PhysicalDeviceObject,
                              &StackLocation,
                              NULL);
}

NTSTATUS
PiIrpCancelStopDevice(
    _In_ PDEVICE_NODE DeviceNode)
{
    IO_STACK_LOCATION StackLocation;

    PAGED_CODE();

    RtlZeroMemory(&StackLocation, sizeof(StackLocation));

    StackLocation.MajorFunction = IRP_MJ_PNP;
    StackLocation.MinorFunction = IRP_MN_CANCEL_STOP_DEVICE;

    return IopSynchronousCall(DeviceNode->PhysicalDeviceObject,
                              &StackLocation,
                              NULL);
}

NTSTATUS
PiIrpQueryDeviceRelations(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ DEVICE_RELATION_TYPE Type)
{
    IO_STACK_LOCATION StackLocation;
    PDEVICE_RELATIONS Relations = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    RtlZeroMemory(&StackLocation, sizeof(StackLocation));

    StackLocation.MajorFunction = IRP_MJ_PNP;
    StackLocation.MinorFunction = IRP_MN_QUERY_DEVICE_RELATIONS;
    StackLocation.Parameters.QueryDeviceRelations.Type = Type;

    Status = IopSynchronousCall(DeviceNode->PhysicalDeviceObject,
                                &StackLocation,
                                (PVOID *)&Relations);
    if (!NT_SUCCESS(Status)) return Status;

    DeviceNode->OverUsed1.PendingDeviceRelations = Relations;

    return STATUS_SUCCESS;
}

NTSTATUS
PiIrpQueryResources(
    _In_ PDEVICE_NODE DeviceNode,
    _Out_ PCM_RESOURCE_LIST *Resources)
{
    IO_STACK_LOCATION StackLocation;
    NTSTATUS Status;

    PAGED_CODE();

    *Resources = NULL;

    RtlZeroMemory(&StackLocation, sizeof(StackLocation));

    StackLocation.MajorFunction = IRP_MJ_PNP;
    StackLocation.MinorFunction = IRP_MN_QUERY_RESOURCES;

    Status = IopSynchronousCall(DeviceNode->PhysicalDeviceObject,
                                &StackLocation,
                                (PVOID *)Resources);
    if (Status == STATUS_NOT_SUPPORTED) return STATUS_SUCCESS;

    return Status;
}

NTSTATUS
PiIrpQueryResourceRequirements(
    _In_ PDEVICE_NODE DeviceNode,
    _Out_ PIO_RESOURCE_REQUIREMENTS_LIST *Resources)
{
    IO_STACK_LOCATION StackLocation;
    NTSTATUS Status;

    PAGED_CODE();

    *Resources = NULL;

    RtlZeroMemory(&StackLocation, sizeof(StackLocation));

    StackLocation.MajorFunction = IRP_MJ_PNP;
    StackLocation.MinorFunction = IRP_MN_QUERY_RESOURCE_REQUIREMENTS;

    Status = IopSynchronousCall(DeviceNode->PhysicalDeviceObject,
                                &StackLocation,
                                (PVOID *)Resources);
    if (Status == STATUS_NOT_SUPPORTED) return STATUS_SUCCESS;

    return Status;
}

NTSTATUS
PiIrpQueryDeviceText(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ LCID LocaleId,
    _In_ DEVICE_TEXT_TYPE Type,
    _Out_ PWSTR *DeviceText)
{
    IO_STACK_LOCATION StackLocation;

    PAGED_CODE();

    *DeviceText = NULL;

    RtlZeroMemory(&StackLocation, sizeof(StackLocation));

    StackLocation.MajorFunction = IRP_MJ_PNP;
    StackLocation.MinorFunction = IRP_MN_QUERY_DEVICE_TEXT;
    StackLocation.Parameters.QueryDeviceText.DeviceTextType = Type;
    StackLocation.Parameters.QueryDeviceText.LocaleId = LocaleId;

    return IopSynchronousCall(DeviceNode->PhysicalDeviceObject,
                              &StackLocation,
                              (PVOID *)DeviceText);
}

NTSTATUS
PiIrpQueryPnPDeviceState(
    _In_ PDEVICE_NODE DeviceNode,
    _Out_ PPNP_DEVICE_STATE DeviceState)
{
    IO_STACK_LOCATION StackLocation;
    PVOID Information = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    *DeviceState = 0;

    RtlZeroMemory(&StackLocation, sizeof(StackLocation));

    StackLocation.MajorFunction = IRP_MJ_PNP;
    StackLocation.MinorFunction = IRP_MN_QUERY_PNP_DEVICE_STATE;

    Status = IopSynchronousCall(DeviceNode->PhysicalDeviceObject,
                                &StackLocation,
                                &Information);
    if (!NT_SUCCESS(Status)) return Status;

    *DeviceState = (PNP_DEVICE_STATE)(ULONG_PTR)Information;

    return STATUS_SUCCESS;
}

/* A removable device expects surprise removal on USB, 1394 and PCMCIA and orderly removal elsewhere; a fixed one keeps its parent's policy, as Windows 11 reports */
VOID
PiUpdateRemovalPolicy(
    _In_ PDEVICE_NODE DeviceNode,
    _In_ BOOLEAN Removable)
{
    DEVICE_REMOVAL_POLICY Policy = RemovalPolicyExpectNoRemoval;
    GUID BusTypeGuid;

    PAGED_CODE();

    if (Removable)
    {
        Policy = RemovalPolicyExpectOrderlyRemoval;

        if (NT_SUCCESS(PnpBusTypeGuidGet(DeviceNode->ChildBusTypeIndex, &BusTypeGuid)) &&
            (IsEqualGUID(&BusTypeGuid, &GUID_BUS_TYPE_USB) ||
             IsEqualGUID(&BusTypeGuid, &GUID_BUS_TYPE_1394) ||
             IsEqualGUID(&BusTypeGuid, &GUID_BUS_TYPE_PCMCIA)))
        {
            Policy = RemovalPolicyExpectSurpriseRemoval;
        }
    }
    else if (DeviceNode->Parent != NULL &&
             DeviceNode->Parent->HardwareRemovalPolicy > RemovalPolicyExpectNoRemoval)
    {
        Policy = DeviceNode->Parent->HardwareRemovalPolicy;
    }

    DeviceNode->HardwareRemovalPolicy = (UCHAR)Policy;
    DeviceNode->RemovalPolicy = (UCHAR)Policy;
}

NTSTATUS
NTAPI
IopQueryDeviceCapabilities(
    PDEVICE_NODE DeviceNode,
    PDEVICE_CAPABILITIES DeviceCaps)
{
    IO_STACK_LOCATION StackLocation;
    NTSTATUS Status;
    ULONG Index;

    PAGED_CODE();

    RtlZeroMemory(DeviceCaps, sizeof(DEVICE_CAPABILITIES));

    DeviceCaps->Size = sizeof(DEVICE_CAPABILITIES);
    DeviceCaps->Version = 1;
    DeviceCaps->Address = (ULONG)-1;
    DeviceCaps->UINumber = (ULONG)-1;

    for (Index = 0; Index < POWER_SYSTEM_MAXIMUM; Index++)
    {
        DeviceCaps->DeviceState[Index] = PowerDeviceUnspecified;
    }

    DeviceCaps->DeviceState[PowerSystemWorking] = PowerDeviceD0;

    RtlZeroMemory(&StackLocation, sizeof(StackLocation));

    StackLocation.MajorFunction = IRP_MJ_PNP;
    StackLocation.MinorFunction = IRP_MN_QUERY_CAPABILITIES;
    StackLocation.Parameters.DeviceCapabilities.Capabilities = DeviceCaps;

    Status = IopSynchronousCall(DeviceNode->PhysicalDeviceObject,
                                &StackLocation,
                                NULL);
    if (!NT_SUCCESS(Status)) return Status;

    if (DeviceCaps->NoDisplayInUI)
    {
        IopDeviceNodeSetUserFlag(DeviceNode, DNUF_DONT_SHOW_IN_UI);
    }
    else
    {
        IopDeviceNodeClearUserFlag(DeviceNode, DNUF_DONT_SHOW_IN_UI);
    }

    /* TODO: the bit layout of DEVICE_NODE.CapabilityFlags is not published */

    PiUpdateRemovalPolicy(DeviceNode, DeviceCaps->Removable);

    return STATUS_SUCCESS;
}

NTSTATUS
PiQueryRemoveDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Out_ PPI_QUERY_REMOVE_DATA RemoveData)
{
    IO_STACK_LOCATION StackLocation;
    PDEVICE_NODE DeviceNode;
    PVOID Information = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    RemoveData->VetoType = PNP_VetoTypeUnknown;
    RtlInitEmptyUnicodeString(&RemoveData->VetoName, NULL, 0);

    RtlZeroMemory(&StackLocation, sizeof(StackLocation));

    StackLocation.MajorFunction = IRP_MJ_PNP;
    StackLocation.MinorFunction = IRP_MN_QUERY_REMOVE_DEVICE;

    Status = IopSynchronousCall(DeviceObject, &StackLocation, &Information);
    if (NT_SUCCESS(Status)) return STATUS_SUCCESS;

    /* Whoever answered the query keeps the device: everything the query
     * reached is told to put it back the way it was */
    RtlZeroMemory(&StackLocation, sizeof(StackLocation));
    StackLocation.MajorFunction = IRP_MJ_PNP;
    StackLocation.MinorFunction = IRP_MN_CANCEL_REMOVE_DEVICE;
    IopSynchronousCall(DeviceObject, &StackLocation, &Information);

    RemoveData->VetoType = PNP_VetoDevice;

    DeviceNode = IopGetDeviceNode(DeviceObject);
    if (DeviceNode != NULL) RemoveData->VetoName = DeviceNode->InstancePath;

    return STATUS_PLUGPLAY_QUERY_VETOED;
}

/* PUBLIC FUNCTIONS ***********************************************************/

VOID
NTAPI
IoInvalidateDeviceRelations(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ DEVICE_RELATION_TYPE Type)
{
    PDEVICE_NODE DeviceNode;

    DeviceNode = IopGetDeviceNode(DeviceObject);
    if (DeviceNode == NULL) return;

    if (Type != BusRelations)
    {
        /* TODO: only bus relations drive a re-enumeration */
        return;
    }

    PiSetDevNodeFlag(DeviceNode, DNF_REENUMERATE);

    PiQueueDeviceAction(DeviceObject,
                        PiActionEnumDeviceTree,
                        NULL,
                        NULL);
}

NTSTATUS
NTAPI
IoSynchronousInvalidateDeviceRelations(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ DEVICE_RELATION_TYPE Type)
{
    PDEVICE_NODE DeviceNode;

    PAGED_CODE();

    DeviceNode = IopGetDeviceNode(DeviceObject);
    if (DeviceNode == NULL) return STATUS_INVALID_DEVICE_REQUEST;

    if (Type != BusRelations) return STATUS_NOT_SUPPORTED;

    PiSetDevNodeFlag(DeviceNode, DNF_REENUMERATE);

    return PiPerformSyncDeviceAction(DeviceObject, PiActionEnumDeviceTree);
}

VOID
NTAPI
IoInvalidateDeviceState(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PDEVICE_NODE DeviceNode;

    DeviceNode = IopGetDeviceNode(PhysicalDeviceObject);
    if (DeviceNode == NULL) return;

    PiQueueDeviceAction(PhysicalDeviceObject,
                        PiActionQueryState,
                        NULL,
                        NULL);
}

VOID
NTAPI
IoRequestDeviceEject(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PAGED_CODE();

    if (IopGetDeviceNode(PhysicalDeviceObject) == NULL) return;

    PiQueueDeviceAction(PhysicalDeviceObject,
                        PiActionEjectDevice,
                        NULL,
                        NULL);
}
