/*
 * PROJECT:     LiberNT LoongArch64 HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Platform bus: PCIe root complex and console UARTs as PnP children
 *
 * The HAL's PnP driver enumerates the machine's built-in devices so the
 * ordinary drivers can bind to them:
 *
 *   ACPI\PNP0A03  the PCIe root complex (pci.sys), with the bus range;
 *   ACPI\PNP0501  the four 16550 register files (serial.sys), each with its
 *                 memory window and level-triggered line.
 *
 * There is no ACPI namespace and no firmware device tree, so the child list is
 * the platform table the HAL already validated. The device IDs are the
 * conventional ones for these two device classes; nothing here claims that an
 * ACPI namespace exists (HalpLa64QuerySystemInformation reports
 * HalAcpiAuditInformation as not found for exactly that reason).
 */

#include <ntifs.h>
#include <ndk/iofuncs.h>
#include "halp.h"

#define HALP_LA64_PLATFORM_FDO 0x4C50464FUL
#define HALP_LA64_PLATFORM_PDO 0x4C505044UL
#define HALP_LA64_PLATFORM_TAG 'bPvL'
#define HALP_LA64_PLATFORM_MAX_CHILDREN 8

#define HALP_LA64_CHILD_PCI  0
#define HALP_LA64_CHILD_UART 1

typedef struct _HALP_LA64_PLATFORM_EXTENSION
{
    ULONG Type;
    PDEVICE_OBJECT LowerDevice;
    PDEVICE_OBJECT Pdo;
    PDEVICE_OBJECT Controller;
    ULONG ChildKind;
    ULONG ChildIndex;
    ULONG FirstBus;
    ULONG LastBus;
    ULONG ChildCount;
    PDEVICE_OBJECT Children[HALP_LA64_PLATFORM_MAX_CHILDREN];
} HALP_LA64_PLATFORM_EXTENSION;

static PDRIVER_OBJECT HalpLa64PlatformDriver;

static
NTSTATUS
HalpLa64CopyId(
    _In_ PCWSTR Source,
    _In_ SIZE_T Size,
    _Out_ PULONG_PTR Information)
{
    PWCHAR Copy = ExAllocatePoolWithTag(PagedPool, Size, HALP_LA64_PLATFORM_TAG);

    if (!Copy)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(Copy, Source, Size);
    *Information = (ULONG_PTR)Copy;
    return STATUS_SUCCESS;
}

static
NTSTATUS
HalpLa64PciResources(
    _In_ HALP_LA64_PLATFORM_EXTENSION *Extension,
    _Out_ PULONG_PTR Information)
{
    PCM_RESOURCE_LIST List = ExAllocatePoolWithTag(PagedPool, sizeof(*List),
                                                   HALP_LA64_PLATFORM_TAG);
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Resource;

    if (!List)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(List, sizeof(*List));
    List->Count = 1;
    List->List[0].InterfaceType = PCIBus;
    List->List[0].BusNumber = Extension->FirstBus;
    List->List[0].PartialResourceList.Version = 1;
    List->List[0].PartialResourceList.Revision = 1;
    List->List[0].PartialResourceList.Count = 1;
    Resource = &List->List[0].PartialResourceList.PartialDescriptors[0];
    Resource->Type = CmResourceTypeBusNumber;
    Resource->ShareDisposition = CmResourceShareDeviceExclusive;
    Resource->u.BusNumber.Start = Extension->FirstBus;
    Resource->u.BusNumber.Length = Extension->LastBus - Extension->FirstBus + 1;
    *Information = (ULONG_PTR)List;
    return STATUS_SUCCESS;
}

static
NTSTATUS
HalpLa64PciRequirements(
    _In_ HALP_LA64_PLATFORM_EXTENSION *Extension,
    _Out_ PULONG_PTR Information)
{
    PIO_RESOURCE_REQUIREMENTS_LIST List;
    PIO_RESOURCE_DESCRIPTOR Resource;

    List = ExAllocatePoolWithTag(PagedPool, sizeof(*List), HALP_LA64_PLATFORM_TAG);
    if (!List)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(List, sizeof(*List));
    List->ListSize = sizeof(*List);
    List->InterfaceType = PCIBus;
    List->BusNumber = Extension->FirstBus;
    List->AlternativeLists = 1;
    List->List[0].Version = 1;
    List->List[0].Revision = 1;
    List->List[0].Count = 1;
    Resource = &List->List[0].Descriptors[0];
    Resource->Type = CmResourceTypeBusNumber;
    Resource->ShareDisposition = CmResourceShareDeviceExclusive;
    Resource->u.BusNumber.Length = Extension->LastBus - Extension->FirstBus + 1;
    Resource->u.BusNumber.MinBusNumber = Extension->FirstBus;
    Resource->u.BusNumber.MaxBusNumber = Extension->LastBus;
    *Information = (ULONG_PTR)List;
    return STATUS_SUCCESS;
}

static
NTSTATUS
HalpLa64UartResources(
    _In_ HALP_LA64_PLATFORM_EXTENSION *Extension,
    _Out_ PULONG_PTR Information)
{
    PCM_RESOURCE_LIST List;
    CM_PARTIAL_RESOURCE_DESCRIPTOR Memory, Interrupt;

    if (!HalpLa64GetUartResource(Extension->ChildIndex, &Memory, &Interrupt))
        return STATUS_NO_SUCH_DEVICE;
    List = ExAllocatePoolWithTag(PagedPool,
                                 FIELD_OFFSET(CM_RESOURCE_LIST,
                                              List[0].PartialResourceList.PartialDescriptors[2]),
                                 HALP_LA64_PLATFORM_TAG);
    if (!List)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(List,
                  FIELD_OFFSET(CM_RESOURCE_LIST,
                               List[0].PartialResourceList.PartialDescriptors[2]));
    List->Count = 1;
    List->List[0].InterfaceType = Internal;
    List->List[0].PartialResourceList.Version = 1;
    List->List[0].PartialResourceList.Revision = 1;
    List->List[0].PartialResourceList.Count = 2;
    List->List[0].PartialResourceList.PartialDescriptors[0] = Memory;
    List->List[0].PartialResourceList.PartialDescriptors[1] = Interrupt;
    *Information = (ULONG_PTR)List;
    return STATUS_SUCCESS;
}

static
NTSTATUS
HalpLa64UartRequirements(
    _In_ HALP_LA64_PLATFORM_EXTENSION *Extension,
    _Out_ PULONG_PTR Information)
{
    PIO_RESOURCE_REQUIREMENTS_LIST List;
    PIO_RESOURCE_DESCRIPTOR Resource;
    CM_PARTIAL_RESOURCE_DESCRIPTOR Memory, Interrupt;
    SIZE_T Size = FIELD_OFFSET(IO_RESOURCE_REQUIREMENTS_LIST,
                               List[0].Descriptors[2]);

    if (!HalpLa64GetUartResource(Extension->ChildIndex, &Memory, &Interrupt))
        return STATUS_NO_SUCH_DEVICE;
    List = ExAllocatePoolWithTag(PagedPool, Size, HALP_LA64_PLATFORM_TAG);
    if (!List)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(List, Size);
    List->ListSize = (ULONG)Size;
    List->InterfaceType = Internal;
    List->AlternativeLists = 1;
    List->List[0].Version = 1;
    List->List[0].Revision = 1;
    List->List[0].Count = 2;

    Resource = &List->List[0].Descriptors[0];
    Resource->Option = 0;
    Resource->Type = CmResourceTypeMemory;
    Resource->ShareDisposition = CmResourceShareDeviceExclusive;
    Resource->Flags = CM_RESOURCE_MEMORY_READ_WRITE;
    Resource->u.Memory.Length = Memory.u.Memory.Length;
    Resource->u.Memory.Alignment = 8;
    Resource->u.Memory.MinimumAddress = Memory.u.Memory.Start;
    Resource->u.Memory.MaximumAddress.QuadPart =
        Memory.u.Memory.Start.QuadPart + Memory.u.Memory.Length - 1;

    Resource = &List->List[0].Descriptors[1];
    Resource->Option = 0;
    Resource->Type = CmResourceTypeInterrupt;
    Resource->ShareDisposition = CmResourceShareShared;
    Resource->Flags = CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE;
    Resource->u.Interrupt.MinimumVector = Interrupt.u.Interrupt.Vector;
    Resource->u.Interrupt.MaximumVector = Interrupt.u.Interrupt.Vector;
    *Information = (ULONG_PTR)List;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
HalpLa64PdoPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIO_STACK_LOCATION Stack,
    _Inout_ PIRP Irp)
{
    HALP_LA64_PLATFORM_EXTENSION *Extension = DeviceObject->DeviceExtension;
    NTSTATUS Status = STATUS_SUCCESS;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_QUERY_ID:
            switch (Stack->Parameters.QueryId.IdType)
            {
                case BusQueryDeviceID:
                    if (Extension->ChildKind == HALP_LA64_CHILD_PCI)
                    {
                        static const WCHAR Id[] = L"ACPI\\PNP0A03";
                        Status = HalpLa64CopyId(Id, sizeof(Id), &Irp->IoStatus.Information);
                    }
                    else
                    {
                        static const WCHAR Id[] = L"ACPI\\PNP0501";
                        Status = HalpLa64CopyId(Id, sizeof(Id), &Irp->IoStatus.Information);
                    }
                    break;

                case BusQueryHardwareIDs:
                    if (Extension->ChildKind == HALP_LA64_CHILD_PCI)
                    {
                        static const WCHAR Ids[] = L"*PNP0A03\0";
                        Status = HalpLa64CopyId(Ids, sizeof(Ids), &Irp->IoStatus.Information);
                    }
                    else
                    {
                        static const WCHAR Ids[] = L"*PNP0501\0";
                        Status = HalpLa64CopyId(Ids, sizeof(Ids), &Irp->IoStatus.Information);
                    }
                    break;

                case BusQueryCompatibleIDs:
                    if (Extension->ChildKind == HALP_LA64_CHILD_PCI)
                    {
                        static const WCHAR Ids[] = L"PNP0A03\0";
                        Status = HalpLa64CopyId(Ids, sizeof(Ids), &Irp->IoStatus.Information);
                    }
                    break;

                case BusQueryInstanceID:
                {
                    WCHAR Id[8];

                    Id[0] = (WCHAR)(L'0' + (Extension->ChildIndex % 10));
                    Id[1] = UNICODE_NULL;
                    Status = HalpLa64CopyId(Id, sizeof(WCHAR) * 2,
                                            &Irp->IoStatus.Information);
                    break;
                }

                default:
                    Status = STATUS_NOT_SUPPORTED;
                    break;
            }
            break;

        case IRP_MN_QUERY_RESOURCES:
            Status = (Extension->ChildKind == HALP_LA64_CHILD_PCI) ?
                     HalpLa64PciResources(Extension, &Irp->IoStatus.Information) :
                     HalpLa64UartResources(Extension, &Irp->IoStatus.Information);
            break;

        case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
            Status = (Extension->ChildKind == HALP_LA64_CHILD_PCI) ?
                     HalpLa64PciRequirements(Extension, &Irp->IoStatus.Information) :
                     HalpLa64UartRequirements(Extension, &Irp->IoStatus.Information);
            break;

        case IRP_MN_QUERY_CAPABILITIES:
        {
            PDEVICE_CAPABILITIES Capabilities =
                Stack->Parameters.DeviceCapabilities.Capabilities;

            if (!Capabilities || Capabilities->Version != 1 ||
                Capabilities->Size < sizeof(*Capabilities))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            Capabilities->UniqueID = TRUE;
            Capabilities->Address = Extension->ChildIndex;
            Capabilities->UINumber = Extension->ChildIndex;
            Capabilities->SilentInstall = TRUE;
            Capabilities->SurpriseRemovalOK = FALSE;
            Status = STATUS_SUCCESS;
            break;
        }

        case IRP_MN_START_DEVICE:
        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
        case IRP_MN_STOP_DEVICE:
            /* These devices are built in: they are always started and are
             * never removed. */
            Status = STATUS_SUCCESS;
            break;

        default:
            Status = STATUS_NOT_SUPPORTED;
            break;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
HalpLa64PlatformPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    HALP_LA64_PLATFORM_EXTENSION *Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status = STATUS_SUCCESS;

    if (Extension->Type == HALP_LA64_PLATFORM_PDO)
        return HalpLa64PdoPnp(DeviceObject, Stack, Irp);

    if (Stack->MinorFunction == IRP_MN_QUERY_DEVICE_RELATIONS &&
        Stack->Parameters.QueryDeviceRelations.Type == BusRelations)
    {
        PDEVICE_RELATIONS Relations;
        ULONG Index;

        Relations = ExAllocatePoolWithTag(PagedPool,
                                          FIELD_OFFSET(DEVICE_RELATIONS, Objects) +
                                          max(Extension->ChildCount, 1) *
                                          sizeof(PDEVICE_OBJECT),
                                          HALP_LA64_PLATFORM_TAG);
        if (!Relations)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Complete;
        }
        Relations->Count = 0;
        for (Index = 0; Index < Extension->ChildCount; ++Index)
        {
            Relations->Objects[Relations->Count++] = Extension->Children[Index];
            ObReferenceObject(Extension->Children[Index]);
        }
        Irp->IoStatus.Information = (ULONG_PTR)Relations;
        goto Complete;
    }

    if (Extension->LowerDevice)
    {
        IoSkipCurrentIrpStackLocation(Irp);
        return IoCallDriver(Extension->LowerDevice, Irp);
    }

Complete:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
HalpLa64PlatformPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    HALP_LA64_PLATFORM_EXTENSION *Extension = DeviceObject->DeviceExtension;

    if (Extension->Type == HALP_LA64_PLATFORM_FDO && Extension->LowerDevice)
    {
        PoStartNextPowerIrp(Irp);
        IoSkipCurrentIrpStackLocation(Irp);
        return IoCallDriver(Extension->LowerDevice, Irp);
    }
    if (Extension->Type == HALP_LA64_PLATFORM_PDO)
        PoStartNextPowerIrp(Irp);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
HalpLa64PlatformDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    HALP_LA64_PLATFORM_EXTENSION *Extension = DeviceObject->DeviceExtension;

    if (Extension->Type == HALP_LA64_PLATFORM_FDO && Extension->LowerDevice)
    {
        IoSkipCurrentIrpStackLocation(Irp);
        return IoCallDriver(Extension->LowerDevice, Irp);
    }
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
HalpLa64CreateChild(
    _In_ PDRIVER_OBJECT DriverObject,
    _Inout_ HALP_LA64_PLATFORM_EXTENSION *Parent,
    _In_ ULONG Kind,
    _In_ ULONG Index)
{
    PDEVICE_OBJECT Child;
    HALP_LA64_PLATFORM_EXTENSION *Extension;
    NTSTATUS Status;

    if (Parent->ChildCount >= HALP_LA64_PLATFORM_MAX_CHILDREN)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = IoCreateDevice(DriverObject, sizeof(*Extension), NULL,
                            FILE_DEVICE_CONTROLLER, FILE_AUTOGENERATED_DEVICE_NAME,
                            FALSE, &Child);
    if (!NT_SUCCESS(Status))
        return Status;
    Extension = Child->DeviceExtension;
    RtlZeroMemory(Extension, sizeof(*Extension));
    Extension->Type = HALP_LA64_PLATFORM_PDO;
    Extension->ChildKind = Kind;
    Extension->ChildIndex = Index;
    Extension->FirstBus = Parent->FirstBus;
    Extension->LastBus = Parent->LastBus;
    Extension->Controller = Parent->Pdo;
    Parent->Children[Parent->ChildCount++] = Child;
    Child->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
HalpLa64PlatformAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    HALP_LA64_PLATFORM_EXTENSION *Extension;
    PDEVICE_OBJECT Fdo;
    NTSTATUS Status;
    ULONG FirstBus = 0, LastBus = 0, Index;
    BOOLEAN HasPci = HalpLa64GetPciBusRange(&FirstBus, &LastBus);

    Status = IoCreateDevice(DriverObject, sizeof(*Extension), NULL,
                            FILE_DEVICE_BUS_EXTENDER, 0, FALSE, &Fdo);
    if (!NT_SUCCESS(Status))
        return Status;
    Extension = Fdo->DeviceExtension;
    RtlZeroMemory(Extension, sizeof(*Extension));
    Extension->Type = HALP_LA64_PLATFORM_FDO;
    Extension->Pdo = PhysicalDeviceObject;
    Extension->FirstBus = FirstBus;
    Extension->LastBus = LastBus;
    Extension->LowerDevice = IoAttachDeviceToDeviceStack(Fdo, PhysicalDeviceObject);
    if (!Extension->LowerDevice)
    {
        IoDeleteDevice(Fdo);
        return STATUS_NO_SUCH_DEVICE;
    }

    if (HasPci)
        HalpLa64CreateChild(DriverObject, Extension, HALP_LA64_CHILD_PCI, 0);
    for (Index = 0; Index < HalpLa64Platform.UartCount; ++Index)
        HalpLa64CreateChild(DriverObject, Extension, HALP_LA64_CHILD_UART, Index);

    Fdo->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
HalpLa64PlatformDriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    PDEVICE_OBJECT RootDevice = NULL;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(RegistryPath);
    HalpLa64PlatformDriver = DriverObject;
    DriverObject->DriverExtension->AddDevice = HalpLa64PlatformAddDevice;
    DriverObject->MajorFunction[IRP_MJ_PNP] = HalpLa64PlatformPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = HalpLa64PlatformPower;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = HalpLa64PlatformDeviceControl;

    Status = IoReportDetectedDevice(DriverObject, InterfaceTypeUndefined,
                                    MAXULONG, MAXULONG, NULL, NULL,
                                    FALSE, &RootDevice);
    if (!NT_SUCCESS(Status))
        return Status;
    RootDevice->Flags &= ~DO_DEVICE_INITIALIZING;
    return HalpLa64PlatformAddDevice(DriverObject, RootDevice);
}

NTSTATUS
NTAPI
HaliInitPnpDriver(VOID)
{
    UNICODE_STRING DriverName = RTL_CONSTANT_STRING(L"\\Driver\\LA64_PLATFORM");

    /* Nothing to enumerate when the platform table has neither a PCIe host
     * nor a console UART. */
    if (!HalpLa64PciHostPresent() && !HalpLa64Platform.UartCount)
        return STATUS_SUCCESS;
    return IoCreateDriver(&DriverName, HalpLa64PlatformDriverEntry);
}
