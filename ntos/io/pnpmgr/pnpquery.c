/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The questions the PnP manager puts to a device stack
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
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
    Irp->UserIosb = &IoStatusBlock;
    Irp->UserEvent = &Event;
    Irp->Flags = IRP_SYNCHRONOUS_API;
    Irp->RequestorMode = KernelMode;
    Irp->Tail.Overlay.Thread = PsGetCurrentThread();

    StackPtr = IoGetNextIrpStackLocation(Irp);

    RtlCopyMemory(StackPtr, IoStackLocation, sizeof(IO_STACK_LOCATION));

    StackPtr->MajorFunction = IRP_MJ_PNP;
    StackPtr->DeviceObject = TopDevice;

    Status = IoCallDriver(TopDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatusBlock.Status;
    }

    if (Information != NULL)
    {
        *Information = (PVOID)IoStatusBlock.Information;
    }

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

    ASSERT(DeviceNode->State == DeviceNodeResourcesAssigned);

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

    if (DeviceCaps->Removable)
    {
        DeviceNode->RemovalPolicy =
            DeviceCaps->SurpriseRemovalOK
                ? RemovalPolicyExpectSurpriseRemoval
                : RemovalPolicyExpectOrderlyRemoval;
    }
    else
    {
        DeviceNode->RemovalPolicy = RemovalPolicyExpectNoRemoval;
    }

    DeviceNode->HardwareRemovalPolicy = DeviceNode->RemovalPolicy;

    return STATUS_SUCCESS;
}

NTSTATUS
PiQueryRemoveDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Out_ PPI_QUERY_REMOVE_DATA RemoveData)
{
    IO_STACK_LOCATION StackLocation;
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

    RemoveData->VetoType = PNP_VetoDevice;

    return Status;
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
