/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Letting a legacy driver report what it found by itself
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#include <umpnpmgr/sysguid.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

static
NTSTATUS
IopCreateDetectedDeviceNode(
    _In_ PDRIVER_OBJECT DriverObject,
    _Out_ PDEVICE_OBJECT *PhysicalDeviceObject,
    _Out_ PDEVICE_NODE *DeviceNode)
{
    PEXTENDED_DRIVER_EXTENSION Extension;
    UNICODE_STRING InstancePath;
    UNICODE_STRING ServiceName;
    PDEVICE_OBJECT Pdo = NULL;
    PDEVICE_NODE Node;
    NTSTATUS Status;

    PAGED_CODE();

    *PhysicalDeviceObject = NULL;
    *DeviceNode = NULL;

    Extension = IoGetDrvObjExtension(DriverObject);

    if (Extension->ServiceKeyName.Length != 0)
    {
        ServiceName = Extension->ServiceKeyName;
    }
    else if (DriverObject->DriverName.Length != 0)
    {
        ServiceName = DriverObject->DriverName;
    }
    else
    {
        return STATUS_INVALID_PARAMETER;
    }

    RtlInitEmptyUnicodeString(&InstancePath, NULL, 0);

    Status = PnpRootCreateDevice(&ServiceName, &Pdo, &InstancePath);
    if (!NT_SUCCESS(Status)) return Status;

    Node = PipAllocateDeviceNode(Pdo);
    if (Node == NULL)
    {
        if (InstancePath.Buffer != NULL) ExFreePool(InstancePath.Buffer);
        PnpRootRetireDevice(Pdo);
        IoDeleteDevice(Pdo);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Node->InstancePath = InstancePath;

    PiSetDevNodeFlag(Node,
                     DNF_MADEUP | DNF_ENUMERATED | DNF_IDS_QUERIED |
                         DNF_LEGACY_DRIVER);

    if (IopRootDeviceNode != NULL) PiInsertDevNode(Node, IopRootDeviceNode);

    *PhysicalDeviceObject = Pdo;
    *DeviceNode = Node;

    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
IoReportDetectedDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ INTERFACE_TYPE LegacyBusType,
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _In_opt_ PCM_RESOURCE_LIST ResourceList,
    _In_opt_ PIO_RESOURCE_REQUIREMENTS_LIST ResourceRequirements,
    _In_ BOOLEAN ResourceAssigned,
    _Inout_ PDEVICE_OBJECT *DeviceObject)
{
    PDEVICE_OBJECT Pdo = NULL;
    PDEVICE_NODE Node = NULL;
    BOOLEAN Created = FALSE;
    NTSTATUS Status;
    HANDLE InstanceKey;
    ULONG Size;

    PAGED_CODE();

    if (*DeviceObject != NULL)
    {
        Pdo = *DeviceObject;
        Node = IopGetDeviceNode(Pdo);
        if (Node == NULL) return STATUS_INVALID_DEVICE_REQUEST;
    }
    else
    {
        Status = IopCreateDetectedDeviceNode(DriverObject, &Pdo, &Node);
        if (!NT_SUCCESS(Status)) return Status;

        Created = TRUE;
    }

    Node->InterfaceType = LegacyBusType;
    Node->BusNumber = BusNumber;
    Node->ChildInterfaceType = LegacyBusType;
    Node->ChildBusNumber = BusNumber;

    UNREFERENCED_PARAMETER(SlotNumber);

    if (ResourceList != NULL)
    {
        Size = PnpDetermineResourceListSize(ResourceList);

        Node->BootResources = ExAllocatePoolZero(PagedPool,
                                                 Size,
                                                 TAG_IO_RESOURCE);
        if (Node->BootResources != NULL)
        {
            RtlCopyMemory(Node->BootResources, ResourceList, Size);
            PiSetDevNodeFlag(Node, DNF_HAS_BOOT_CONFIG);
        }
    }

    if (ResourceRequirements != NULL)
    {
        Node->ResourceRequirements =
            ExAllocatePoolZero(PagedPool,
                               ResourceRequirements->ListSize,
                               TAG_IO_RESOURCE);
        if (Node->ResourceRequirements != NULL)
        {
            RtlCopyMemory(Node->ResourceRequirements,
                          ResourceRequirements,
                          ResourceRequirements->ListSize);
        }
    }
    else
    {
        PiSetDevNodeFlag(Node, DNF_NO_RESOURCE_REQUIRED);
    }

    if (ResourceAssigned && ResourceList != NULL)
    {
        IopResDbReserve(Node, ResourceList, NULL);

        Node->ResourceList = Node->BootResources;
        Node->ResourceListTranslated = Node->BootResources;
    }

    Status = PnpDeviceObjectToDeviceInstance(Pdo,
                                             &InstanceKey,
                                             KEY_ALL_ACCESS);
    if (!NT_SUCCESS(Status))
    {
        UNICODE_STRING KeyPath;
        USHORT Length;
        PWCHAR Buffer;

        static const UNICODE_STRING EnumRoot =
            RTL_CONSTANT_STRING(ENUM_ROOT);

        Length = EnumRoot.Length + sizeof(WCHAR) +
                 Node->InstancePath.Length + sizeof(WCHAR);

        Buffer = ExAllocatePoolZero(PagedPool, Length, TAG_IO);
        if (Buffer != NULL)
        {
            KeyPath.Buffer = Buffer;
            KeyPath.Length = 0;
            KeyPath.MaximumLength = Length;

            RtlAppendUnicodeStringToString(&KeyPath,
                                           (PUNICODE_STRING)&EnumRoot);
            RtlAppendUnicodeToString(&KeyPath, L"\\");
            RtlAppendUnicodeStringToString(&KeyPath, &Node->InstancePath);

            Status = IopCreateDeviceKeyPath(&KeyPath,
                                            REG_OPTION_NON_VOLATILE,
                                            &InstanceKey);

            ExFreePoolWithTag(Buffer, TAG_IO);
        }
    }

    if (NT_SUCCESS(Status))
    {
        UNICODE_STRING ValueName;
        ULONG Value = 1;

        RtlInitUnicodeString(&ValueName, L"Legacy");

        ZwSetValueKey(InstanceKey,
                      &ValueName,
                      0,
                      REG_DWORD,
                      &Value,
                      sizeof(Value));

        IopSetDeviceInstanceData(InstanceKey, Node);

        ZwClose(InstanceKey);
    }

    PiSetDevNodeState(Node, DeviceNodeStarted);

    if (Created) IopQueueDeviceInstallEvent(&GUID_DEVICE_ARRIVAL,
                                            &Node->InstancePath);

    *DeviceObject = Pdo;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
IoReportRootDevice(
    _In_ PDRIVER_OBJECT DriverObject)
{
    PDEVICE_OBJECT Pdo = NULL;
    PDEVICE_NODE Node = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    Status = IopCreateDetectedDeviceNode(DriverObject, &Pdo, &Node);
    if (!NT_SUCCESS(Status)) return Status;

    PiSetDevNodeFlag(Node, DNF_NO_RESOURCE_REQUIRED);
    PiSetDevNodeState(Node, DeviceNodeStarted);

    IopQueueDeviceInstallEvent(&GUID_DEVICE_ARRIVAL, &Node->InstancePath);

    return STATUS_SUCCESS;
}
