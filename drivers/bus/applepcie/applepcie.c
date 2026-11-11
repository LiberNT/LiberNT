/*
 * PROJECT:     LiberNT Apple Silicon PCIe Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Brings up the links of the Apple silicon PCIe controller
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <initguid.h>
#include <wdmguid.h>
#include <reactos/drivers/fdtdrv.h>
#include <reactos/drivers/applepcie.h>
#include <reactos/drivers/applesmc.h>

#define APPLE_PCIE_SMC_WAIT_MS      30000
#define APPLE_PCIE_SMC_STEP_MS      50
#define APPLE_PCIE_TAG              'icPA'
#define APPLE_PCIE_FDO              0x4F444650UL
#define APPLE_PCIE_PDO              0x4F445050UL

typedef struct _APPLE_PCIE_EXTENSION
{
    ULONG Type;
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT LowerDevice;
    PDEVICE_OBJECT PhysicalDevice;
    PDEVICE_OBJECT Child;
    struct _APPLE_PCIE_EXTENSION *Parent;
    BOOLEAN ChildReported;
    BOOLEAN Started;
    PKTHREAD BringUpThread;
    FDT_DRIVER_TREE Tree;
    APPLE_PCIE Pcie;
} APPLE_PCIE_EXTENSION, *PAPPLE_PCIE_EXTENSION;

DRIVER_INITIALIZE DriverEntry;

static
VOID
ApplePcieSleepMs(
    _In_ ULONG Milliseconds)
{
    LARGE_INTEGER Interval;

    Interval.QuadPart = -10000LL * Milliseconds;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

static
NTSTATUS
ApplePcieSmcRequest(
    _In_ PAPPLE_SMC_GPIO Gpio)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(APPLE_SMC_DEVICE_NAME);
    PFILE_OBJECT FileObject;
    PDEVICE_OBJECT SmcDevice;
    IO_STATUS_BLOCK Io;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    Status = IoGetDeviceObjectPointer(&Name, FILE_READ_DATA, &FileObject, &SmcDevice);
    if (!NT_SUCCESS(Status))
        return Status;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_APPLE_SMC_SET_GPIO, SmcDevice, Gpio, sizeof(*Gpio), NULL, 0, TRUE,
                                        &Event, &Io);
    if (!Irp)
    {
        ObDereferenceObject(FileObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Status = IoCallDriver(SmcDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Io.Status;
    }
    ObDereferenceObject(FileObject);
    return Status;
}

static
NTSTATUS
NTAPI
ApplePcieSetSmcGpio(
    _In_opt_ PVOID Context,
    _In_ BOOLEAN LowBank,
    _In_ UCHAR Pin,
    _In_ BOOLEAN Value)
{
    APPLE_SMC_GPIO Gpio;
    ULONG Waited;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Context);
    Gpio.LowBank = LowBank;
    Gpio.Pin = Pin;
    Gpio.Value = Value;
    for (Waited = 0;; Waited += APPLE_PCIE_SMC_STEP_MS)
    {
        Status = ApplePcieSmcRequest(&Gpio);
        if ((Status != STATUS_OBJECT_NAME_NOT_FOUND && Status != STATUS_DEVICE_NOT_READY) ||
            Waited >= APPLE_PCIE_SMC_WAIT_MS)
            break;
        ApplePcieSleepMs(APPLE_PCIE_SMC_STEP_MS);
    }
    DbgPrint("applepcie: SMC GPIO %u of the %s bank set to %u after %lu ms: %lx\n", Pin, LowBank ? "low" : "main",
             Value, Waited, Status);
    return Status;
}

static
VOID
ApplePcieReportPort(
    _In_ PAPPLE_PCIE Pcie,
    _In_ PAPPLE_PCIE_PORT Port,
    _In_ NTSTATUS Status)
{
    ULONG Function, Id, Class;

    Id = ApplePcieReadConfig(Pcie, 0, Port->Index, 0, 0);
    DbgPrint("applepcie: port %lu (root port %04lx:%04lx) bus %lu link %s, %s, waited %lu ms, %lu RID2SID entries: %lx\n",
             Port->Index, Id & 0xFFFF, Id >> 16, Port->Bus, Port->LinkUp ? "up" : "down",
             Port->WasUp ? "kept from the firmware" : "trained here", Port->LinkWaitMs, Port->Rid2SidEntries, Status);
    if (!Port->LinkUp || Port->Bus == MAXULONG)
        return;
    for (Function = 0; Function < 8; ++Function)
    {
        Id = ApplePcieReadConfig(Pcie, Port->Bus, 0, Function, 0);
        if (Id == MAXULONG || Id == 0)
            continue;
        Class = ApplePcieReadConfig(Pcie, Port->Bus, 0, Function, 8);
        DbgPrint("applepcie: bus %lu function %lu is %04lx:%04lx class %06lx\n", Port->Bus, Function, Id & 0xFFFF,
                 Id >> 16, Class >> 8);
    }
}

static NTSTATUS ApplePcieCreateChild(_Inout_ PAPPLE_PCIE_EXTENSION Extension);

static
VOID
NTAPI
ApplePcieBringUp(
    _In_ PVOID Context)
{
    PAPPLE_PCIE_EXTENSION Extension = Context;
    PAPPLE_PCIE Pcie = &Extension->Pcie;
    ULONG Index, Mapped = 0, LinksUp = 0;
    NTSTATUS Status;

    for (Index = 0; Index < Pcie->PortCount; ++Index)
    {
        ApplePcieReportPort(Pcie, &Pcie->Ports[Index], ApplePcieStartPort(&Extension->Tree, Pcie, &Pcie->Ports[Index]));
        if (Pcie->Ports[Index].LinkUp)
        {
            LinksUp++;
            Mapped += ApplePcieMapPortDevices(&Extension->Tree, Pcie, &Pcie->Ports[Index]);
        }
    }
    DbgPrint("applepcie: %lu links up, %lu functions mapped through their DARTs\n", LinksUp, Mapped);
    if (Pcie->WindowCount)
    {
        Status = ApplePcieCreateChild(Extension);
        if (NT_SUCCESS(Status))
            IoInvalidateDeviceRelations(Extension->PhysicalDevice, BusRelations);
        else
            DbgPrint("applepcie: host bridge device not created: %lx\n", Status);
    }
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static
NTSTATUS
ApplePcieStart(
    _Inout_ PAPPLE_PCIE_EXTENSION Extension)
{
    PAPPLE_PCIE Pcie = &Extension->Pcie;
    HANDLE ThreadHandle;
    NTSTATUS Status;

    if (Extension->Started)
        return STATUS_SUCCESS;
    Status = FdtDriverOpen(Extension->PhysicalDevice, &Extension->Tree);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = ApplePcieOpen(&Extension->Tree, Extension->Tree.Node, Pcie);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("applepcie: the controller is not usable: %lx\n", Status);
        ApplePcieClose(Pcie);
        FdtDriverClose(&Extension->Tree);
        return Status;
    }
    Pcie->SetSmcGpio = ApplePcieSetSmcGpio;
    DbgPrint("applepcie: config space %I64x+%I64x, buses %lu-%lu, %lu windows, %lu ports, %lu MSI vectors, pin controller %s\n",
             Pcie->ConfigBase, Pcie->ConfigSize, Pcie->FirstBus, Pcie->LastBus, Pcie->WindowCount, Pcie->PortCount,
             Pcie->MsiVectors, Pcie->Pinctrl.Base ? "mapped" : "missing");
    Status = PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL, ApplePcieBringUp, Extension);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("applepcie: link bring-up thread not started: %lx\n", Status);
        ApplePcieClose(Pcie);
        FdtDriverClose(&Extension->Tree);
        return Status;
    }
    NT_VERIFY(NT_SUCCESS(ObReferenceObjectByHandle(ThreadHandle, THREAD_ALL_ACCESS, NULL, KernelMode,
                                                   (PVOID *)&Extension->BringUpThread, NULL)));
    ZwClose(ThreadHandle);
    Extension->Started = TRUE;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ApplePcieCopyString(
    _In_reads_bytes_(Size) const WCHAR *Source,
    _In_ SIZE_T Size,
    _Out_ PULONG_PTR Information)
{
    PWCHAR Copy = ExAllocatePoolWithTag(PagedPool, Size, APPLE_PCIE_TAG);

    if (!Copy)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(Copy, Source, Size);
    *Information = (ULONG_PTR)Copy;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ApplePcieChildResources(
    _In_ PAPPLE_PCIE Pcie,
    _Out_ PULONG_PTR Information)
{
    SIZE_T Size = FIELD_OFFSET(CM_RESOURCE_LIST, List[0].PartialResourceList.PartialDescriptors) +
                  (1 + Pcie->WindowCount) * sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR);
    PCM_RESOURCE_LIST List = ExAllocatePoolZero(PagedPool, Size, APPLE_PCIE_TAG);
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    ULONG Index;

    if (!List)
        return STATUS_INSUFFICIENT_RESOURCES;
    List->Count = 1;
    List->List[0].InterfaceType = PCIBus;
    List->List[0].BusNumber = Pcie->FirstBus;
    List->List[0].PartialResourceList.Version = 1;
    List->List[0].PartialResourceList.Revision = 1;
    List->List[0].PartialResourceList.Count = 1 + Pcie->WindowCount;
    Descriptor = List->List[0].PartialResourceList.PartialDescriptors;
    Descriptor->Type = CmResourceTypeBusNumber;
    Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
    Descriptor->u.BusNumber.Start = Pcie->FirstBus;
    Descriptor->u.BusNumber.Length = Pcie->LastBus - Pcie->FirstBus + 1;
    for (Index = 0; Index < Pcie->WindowCount; ++Index)
    {
        ++Descriptor;
        Descriptor->Type = CmResourceTypeMemory;
        Descriptor->ShareDisposition = CmResourceShareShared;
        Descriptor->Flags = CM_RESOURCE_MEMORY_READ_WRITE | CM_RESOURCE_MEMORY_WINDOW_DECODE |
                            (Pcie->Windows[Index].Prefetchable ? CM_RESOURCE_MEMORY_PREFETCHABLE : 0);
        Descriptor->u.Memory.Start.QuadPart = (LONGLONG)Pcie->Windows[Index].BusBase;
        Descriptor->u.Memory.Length = Pcie->Windows[Index].Size;
    }
    *Information = (ULONG_PTR)List;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ApplePcieChildRequirements(
    _In_ PAPPLE_PCIE Pcie,
    _Out_ PULONG_PTR Information)
{
    SIZE_T Size = FIELD_OFFSET(IO_RESOURCE_REQUIREMENTS_LIST, List[0].Descriptors) +
                  (1 + Pcie->WindowCount) * sizeof(IO_RESOURCE_DESCRIPTOR);
    PIO_RESOURCE_REQUIREMENTS_LIST List = ExAllocatePoolZero(PagedPool, Size, APPLE_PCIE_TAG);
    PIO_RESOURCE_DESCRIPTOR Descriptor;
    ULONG Index;

    if (!List)
        return STATUS_INSUFFICIENT_RESOURCES;
    List->ListSize = (ULONG)Size;
    List->InterfaceType = PCIBus;
    List->BusNumber = Pcie->FirstBus;
    List->AlternativeLists = 1;
    List->List[0].Version = 1;
    List->List[0].Revision = 1;
    List->List[0].Count = 1 + Pcie->WindowCount;
    Descriptor = List->List[0].Descriptors;
    Descriptor->Type = CmResourceTypeBusNumber;
    Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
    Descriptor->u.BusNumber.MinBusNumber = Pcie->FirstBus;
    Descriptor->u.BusNumber.MaxBusNumber = Pcie->LastBus;
    Descriptor->u.BusNumber.Length = Pcie->LastBus - Pcie->FirstBus + 1;
    for (Index = 0; Index < Pcie->WindowCount; ++Index)
    {
        ++Descriptor;
        Descriptor->Type = CmResourceTypeMemory;
        Descriptor->ShareDisposition = CmResourceShareShared;
        Descriptor->Flags = CM_RESOURCE_MEMORY_READ_WRITE | CM_RESOURCE_MEMORY_WINDOW_DECODE |
                            (Pcie->Windows[Index].Prefetchable ? CM_RESOURCE_MEMORY_PREFETCHABLE : 0);
        Descriptor->u.Memory.Length = Pcie->Windows[Index].Size;
        Descriptor->u.Memory.Alignment = 1;
        Descriptor->u.Memory.MinimumAddress.QuadPart = (LONGLONG)Pcie->Windows[Index].BusBase;
        Descriptor->u.Memory.MaximumAddress.QuadPart =
            (LONGLONG)(Pcie->Windows[Index].BusBase + Pcie->Windows[Index].Size - 1);
    }
    *Information = (ULONG_PTR)List;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ApplePcieChildPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PAPPLE_PCIE_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status = Irp->IoStatus.Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_QUERY_ID:
            switch (Stack->Parameters.QueryId.IdType)
            {
                case BusQueryDeviceID:
                {
                    static const WCHAR Id[] = L"ACPI\\PNP0A03";
                    Status = ApplePcieCopyString(Id, sizeof(Id), &Irp->IoStatus.Information);
                    break;
                }
                case BusQueryHardwareIDs:
                {
                    static const WCHAR Ids[] = L"*PNP0A03\0";
                    Status = ApplePcieCopyString(Ids, sizeof(Ids), &Irp->IoStatus.Information);
                    break;
                }
                case BusQueryInstanceID:
                {
                    static const WCHAR Id[] = L"0";
                    Status = ApplePcieCopyString(Id, sizeof(Id), &Irp->IoStatus.Information);
                    break;
                }
                default:
                    break;
            }
            break;

        case IRP_MN_QUERY_DEVICE_TEXT:
            if (Stack->Parameters.QueryDeviceText.DeviceTextType == DeviceTextDescription)
            {
                static const WCHAR Text[] = L"Apple silicon PCIe root complex";
                Status = ApplePcieCopyString(Text, sizeof(Text), &Irp->IoStatus.Information);
            }
            break;

        case IRP_MN_QUERY_RESOURCES:
            Status = Extension->Parent ? ApplePcieChildResources(&Extension->Parent->Pcie, &Irp->IoStatus.Information)
                                       : STATUS_NO_SUCH_DEVICE;
            break;

        case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
            Status = Extension->Parent ? ApplePcieChildRequirements(&Extension->Parent->Pcie, &Irp->IoStatus.Information)
                                       : STATUS_NO_SUCH_DEVICE;
            break;

        case IRP_MN_QUERY_BUS_INFORMATION:
        {
            PPNP_BUS_INFORMATION Information = ExAllocatePoolWithTag(PagedPool, sizeof(*Information), APPLE_PCIE_TAG);

            if (!Information)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }
            Information->BusTypeGuid = GUID_BUS_TYPE_INTERNAL;
            Information->LegacyBusType = Internal;
            Information->BusNumber = 0;
            Irp->IoStatus.Information = (ULONG_PTR)Information;
            Status = STATUS_SUCCESS;
            break;
        }

        case IRP_MN_QUERY_CAPABILITIES:
        {
            PDEVICE_CAPABILITIES Capabilities = Stack->Parameters.DeviceCapabilities.Capabilities;

            if (!Capabilities || Capabilities->Version != 1 || Capabilities->Size < sizeof(*Capabilities))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            Capabilities->SilentInstall = TRUE;
            Capabilities->Removable = FALSE;
            Capabilities->SurpriseRemovalOK = FALSE;
            Status = STATUS_SUCCESS;
            break;
        }

        case IRP_MN_QUERY_DEVICE_RELATIONS:
            if (Stack->Parameters.QueryDeviceRelations.Type == TargetDeviceRelation)
            {
                PDEVICE_RELATIONS Relations = ExAllocatePoolWithTag(PagedPool, sizeof(*Relations), APPLE_PCIE_TAG);

                if (!Relations)
                {
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                    break;
                }
                Relations->Count = 1;
                Relations->Objects[0] = DeviceObject;
                ObReferenceObject(DeviceObject);
                Irp->IoStatus.Information = (ULONG_PTR)Relations;
                Status = STATUS_SUCCESS;
            }
            break;

        case IRP_MN_START_DEVICE:
        case IRP_MN_STOP_DEVICE:
        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            Status = STATUS_SUCCESS;
            if (!Extension->Parent || !Extension->Parent->ChildReported)
            {
                Irp->IoStatus.Status = Status;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                if (Extension->Parent)
                    Extension->Parent->Child = NULL;
                IoDeleteDevice(DeviceObject);
                return Status;
            }
            break;

        default:
            break;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
ApplePcieCreateChild(
    _Inout_ PAPPLE_PCIE_EXTENSION Extension)
{
    PAPPLE_PCIE_EXTENSION ChildExtension;
    PDEVICE_OBJECT Child;
    NTSTATUS Status;

    if (Extension->Child)
        return STATUS_SUCCESS;
    Status = IoCreateDevice(Extension->Self->DriverObject, sizeof(APPLE_PCIE_EXTENSION), NULL, FILE_DEVICE_BUS_EXTENDER,
                            FILE_AUTOGENERATED_DEVICE_NAME, FALSE, &Child);
    if (!NT_SUCCESS(Status))
        return Status;
    ChildExtension = Child->DeviceExtension;
    RtlZeroMemory(ChildExtension, sizeof(*ChildExtension));
    ChildExtension->Type = APPLE_PCIE_PDO;
    ChildExtension->Self = Child;
    ChildExtension->Parent = Extension;
    Child->Flags &= ~DO_DEVICE_INITIALIZING;
    KeMemoryBarrier();
    Extension->Child = Child;
    Extension->ChildReported = TRUE;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ApplePcieFdoPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PAPPLE_PCIE_EXTENSION Extension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = IoForwardIrpSynchronously(Extension->LowerDevice, Irp) ? Irp->IoStatus.Status
                                                                             : STATUS_UNSUCCESSFUL;
            if (NT_SUCCESS(Status))
                Status = ApplePcieStart(Extension);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_QUERY_DEVICE_RELATIONS:
            if (Stack->Parameters.QueryDeviceRelations.Type == BusRelations)
            {
                PDEVICE_RELATIONS Previous = (PDEVICE_RELATIONS)Irp->IoStatus.Information;
                PDEVICE_RELATIONS Relations;
                ULONG Count = Previous ? Previous->Count : 0;
                ULONG Extra = (Extension->Child && Extension->ChildReported) ? 1 : 0;

                Relations = ExAllocatePoolWithTag(PagedPool, FIELD_OFFSET(DEVICE_RELATIONS, Objects[Count + Extra + 1]),
                                                  APPLE_PCIE_TAG);
                if (!Relations)
                {
                    Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
                    IoCompleteRequest(Irp, IO_NO_INCREMENT);
                    return STATUS_INSUFFICIENT_RESOURCES;
                }
                Relations->Count = Count;
                if (Count)
                    RtlCopyMemory(Relations->Objects, Previous->Objects, Count * sizeof(PDEVICE_OBJECT));
                if (Extra)
                {
                    Relations->Objects[Relations->Count++] = Extension->Child;
                    ObReferenceObject(Extension->Child);
                }
                if (Previous)
                    ExFreePool(Previous);
                Irp->IoStatus.Information = (ULONG_PTR)Relations;
                Irp->IoStatus.Status = STATUS_SUCCESS;
            }
            break;

        case IRP_MN_REMOVE_DEVICE:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(Extension->LowerDevice, Irp);
            if (Extension->BringUpThread)
            {
                KeWaitForSingleObject(Extension->BringUpThread, Executive, KernelMode, FALSE, NULL);
                ObDereferenceObject(Extension->BringUpThread);
                Extension->BringUpThread = NULL;
            }
            Extension->ChildReported = FALSE;
            if (Extension->Child)
            {
                PAPPLE_PCIE_EXTENSION ChildExtension = Extension->Child->DeviceExtension;

                ChildExtension->Parent = NULL;
                IoDeleteDevice(Extension->Child);
                Extension->Child = NULL;
            }
            if (Extension->Started)
            {
                ApplePcieClose(&Extension->Pcie);
                FdtDriverClose(&Extension->Tree);
                Extension->Started = FALSE;
            }
            IoDetachDevice(Extension->LowerDevice);
            IoDeleteDevice(DeviceObject);
            return Status;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        default:
            break;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Extension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
ApplePcieDispatchPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PAPPLE_PCIE_EXTENSION Extension = DeviceObject->DeviceExtension;

    if (Extension->Type == APPLE_PCIE_PDO)
        return ApplePcieChildPnp(DeviceObject, Irp);
    return ApplePcieFdoPnp(DeviceObject, Irp);
}

static
NTSTATUS
NTAPI
ApplePcieDispatchPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PAPPLE_PCIE_EXTENSION Extension = DeviceObject->DeviceExtension;
    NTSTATUS Status;

    PoStartNextPowerIrp(Irp);
    if (Extension->Type == APPLE_PCIE_PDO)
    {
        Status = STATUS_SUCCESS;
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(Extension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
ApplePcieDispatchOther(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PAPPLE_PCIE_EXTENSION Extension = DeviceObject->DeviceExtension;
    NTSTATUS Status;

    if (Extension->Type == APPLE_PCIE_PDO)
    {
        Status = Irp->IoStatus.Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Extension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
ApplePcieAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PAPPLE_PCIE_EXTENSION Extension;
    PDEVICE_OBJECT Fdo;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject, sizeof(APPLE_PCIE_EXTENSION), NULL, FILE_DEVICE_BUS_EXTENDER,
                            FILE_DEVICE_SECURE_OPEN, FALSE, &Fdo);
    if (!NT_SUCCESS(Status))
        return Status;
    Extension = Fdo->DeviceExtension;
    RtlZeroMemory(Extension, sizeof(*Extension));
    Extension->Type = APPLE_PCIE_FDO;
    Extension->Self = Fdo;
    Extension->PhysicalDevice = PhysicalDeviceObject;
    Extension->LowerDevice = IoAttachDeviceToDeviceStack(Fdo, PhysicalDeviceObject);
    if (!Extension->LowerDevice)
    {
        IoDeleteDevice(Fdo);
        return STATUS_NO_SUCH_DEVICE;
    }
    Fdo->Flags |= DO_POWER_PAGABLE;
    Fdo->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    ULONG Index;

    UNREFERENCED_PARAMETER(RegistryPath);
    for (Index = 0; Index <= IRP_MJ_MAXIMUM_FUNCTION; ++Index)
        DriverObject->MajorFunction[Index] = ApplePcieDispatchOther;
    DriverObject->MajorFunction[IRP_MJ_PNP] = ApplePcieDispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = ApplePcieDispatchPower;
    DriverObject->DriverExtension->AddDevice = ApplePcieAddDevice;
    return STATUS_SUCCESS;
}
