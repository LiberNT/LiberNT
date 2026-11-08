/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bus driver whose child is reported missing and deleted while still referenced
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <ntstrsafe.h>

typedef struct _FIXTURE_PARENT
{
    BOOLEAN IsFdo;
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT LowerDevice;
    PDEVICE_OBJECT Pdo;
    PDEVICE_OBJECT Child;
    PIO_WORKITEM WorkItem;
    PIO_WORKITEM FailWorkItem;
    BOOLEAN FailAfterChildRemove;
    BOOLEAN ChildPresent;
    BOOLEAN ChildReferenced;
    BOOLEAN ChildDeleted;
    BOOLEAN FailParent;
    ULONG ChildStarts;
    ULONG ChildSurprises;
    ULONG ChildRemoves;
    ULONG ChildIrpsAfterDelete;
    WCHAR ValuePath[300];
} FIXTURE_PARENT, *PFIXTURE_PARENT;

typedef struct _FIXTURE_CHILD
{
    BOOLEAN IsFdo;
    PFIXTURE_PARENT Parent;
} FIXTURE_CHILD, *PFIXTURE_CHILD;

static WCHAR ServicePath[260];

static VOID FixtureRecord(PFIXTURE_PARENT Parent, PCWSTR Name, ULONG Value)
{
    RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, Parent->ValuePath, Name, REG_DWORD, &Value, sizeof(Value));
}

static NTSTATUS FixtureComplete(PIRP Irp, NTSTATUS Status)
{
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static VOID NTAPI FixtureReportChildMissing(PDEVICE_OBJECT DeviceObject, PVOID Context)
{
    PFIXTURE_PARENT Parent = Context;

    UNREFERENCED_PARAMETER(DeviceObject);

    ObReferenceObject(Parent->Child);
    Parent->ChildReferenced = TRUE;
    Parent->ChildPresent = FALSE;
    IoInvalidateDeviceRelations(Parent->Pdo, BusRelations);
}

static VOID NTAPI FixtureFailParent(PDEVICE_OBJECT DeviceObject, PVOID Context)
{
    PFIXTURE_PARENT Parent = Context;

    UNREFERENCED_PARAMETER(DeviceObject);

    Parent->FailParent = TRUE;
    IoInvalidateDeviceState(Parent->Pdo);
}

static NTSTATUS FixtureChildQueryId(PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    static const WCHAR DeviceId[] = L"KMTEST\\REMOVECHILD";
    static const WCHAR HardwareIds[] = L"KMTEST\\REMOVECHILD\0";
    static const WCHAR InstanceId[] = L"0";
    PCWSTR Source;
    SIZE_T Size;
    PWSTR Buffer;

    switch (Stack->Parameters.QueryId.IdType)
    {
        case BusQueryDeviceID: Source = DeviceId; Size = sizeof(DeviceId); break;
        case BusQueryHardwareIDs: Source = HardwareIds; Size = sizeof(HardwareIds); break;
        case BusQueryInstanceID: Source = InstanceId; Size = sizeof(InstanceId); break;
        default: return FixtureComplete(Irp, Irp->IoStatus.Status);
    }

    Buffer = ExAllocatePoolWithTag(PagedPool, Size, 'CmRK');
    if (!Buffer)
        return FixtureComplete(Irp, STATUS_INSUFFICIENT_RESOURCES);
    RtlCopyMemory(Buffer, Source, Size);
    Irp->IoStatus.Information = (ULONG_PTR)Buffer;
    return FixtureComplete(Irp, STATUS_SUCCESS);
}

static NTSTATUS FixtureChildPnp(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PFIXTURE_PARENT Parent = ((PFIXTURE_CHILD)DeviceObject->DeviceExtension)->Parent;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PDEVICE_CAPABILITIES Capabilities;

    if (Parent->ChildDeleted)
    {
        FixtureRecord(Parent, L"ChildIrpsAfterDelete", ++Parent->ChildIrpsAfterDelete);
        FixtureRecord(Parent, L"ChildLateMinor", Stack->MinorFunction);
        return FixtureComplete(Irp, STATUS_NO_SUCH_DEVICE);
    }

    switch (Stack->MinorFunction)
    {
        case IRP_MN_QUERY_ID:
            return FixtureChildQueryId(Irp);

        case IRP_MN_QUERY_CAPABILITIES:
            Capabilities = Stack->Parameters.DeviceCapabilities.Capabilities;
            Capabilities->RawDeviceOK = TRUE;
            Capabilities->SilentInstall = TRUE;
            Capabilities->Removable = TRUE;
            return FixtureComplete(Irp, STATUS_SUCCESS);

        case IRP_MN_START_DEVICE:
            FixtureRecord(Parent, L"ChildStarts", ++Parent->ChildStarts);
            IoQueueWorkItem(Parent->WorkItem, FixtureReportChildMissing, DelayedWorkQueue, Parent);
            return FixtureComplete(Irp, STATUS_SUCCESS);

        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
        case IRP_MN_STOP_DEVICE:
        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
            return FixtureComplete(Irp, STATUS_SUCCESS);

        case IRP_MN_SURPRISE_REMOVAL:
            FixtureRecord(Parent, L"ChildSurprises", ++Parent->ChildSurprises);
            return FixtureComplete(Irp, STATUS_SUCCESS);

        case IRP_MN_REMOVE_DEVICE:
            FixtureRecord(Parent, L"ChildRemoves", ++Parent->ChildRemoves);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            if (!Parent->ChildPresent)
            {
                Parent->ChildDeleted = TRUE;
                IoDeleteDevice(DeviceObject);
                if (Parent->FailAfterChildRemove)
                    IoQueueWorkItem(Parent->FailWorkItem, FixtureFailParent, DelayedWorkQueue, Parent);
            }
            return STATUS_SUCCESS;

        default:
            return FixtureComplete(Irp, Irp->IoStatus.Status);
    }
}

static NTSTATUS FixtureParentPnp(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PFIXTURE_PARENT Parent = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PDEVICE_OBJECT Lower = Parent->LowerDevice;
    PFIXTURE_CHILD ChildExtension;
    PDEVICE_RELATIONS Relations;
    NTSTATUS Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            if (!IoForwardIrpSynchronously(Lower, Irp))
                return FixtureComplete(Irp, STATUS_UNSUCCESSFUL);
            Status = Irp->IoStatus.Status;
            if (NT_SUCCESS(Status) && !Parent->Child)
            {
                Status = IoCreateDevice(DeviceObject->DriverObject, sizeof(FIXTURE_CHILD), NULL,
                                        FILE_DEVICE_UNKNOWN, FILE_AUTOGENERATED_DEVICE_NAME, FALSE, &Parent->Child);
                if (NT_SUCCESS(Status))
                {
                    ChildExtension = Parent->Child->DeviceExtension;
                    ChildExtension->IsFdo = FALSE;
                    ChildExtension->Parent = Parent;
                    Parent->Child->Flags &= ~DO_DEVICE_INITIALIZING;
                    Parent->ChildPresent = TRUE;
                    IoInvalidateDeviceRelations(Parent->Pdo, BusRelations);
                }
            }
            return FixtureComplete(Irp, Status);

        case IRP_MN_QUERY_DEVICE_RELATIONS:
            if (Stack->Parameters.QueryDeviceRelations.Type != BusRelations)
                break;
            Relations = ExAllocatePoolWithTag(PagedPool, sizeof(DEVICE_RELATIONS), 'CmRK');
            if (!Relations)
                return FixtureComplete(Irp, STATUS_INSUFFICIENT_RESOURCES);
            Relations->Count = 0;
            if (Parent->ChildPresent)
            {
                ObReferenceObject(Parent->Child);
                Relations->Objects[Relations->Count++] = Parent->Child;
            }
            Irp->IoStatus.Information = (ULONG_PTR)Relations;
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_PNP_DEVICE_STATE:
            if (!IoForwardIrpSynchronously(Lower, Irp))
                return FixtureComplete(Irp, STATUS_UNSUCCESSFUL);
            if (Parent->FailParent)
            {
                if (!NT_SUCCESS(Irp->IoStatus.Status))
                    Irp->IoStatus.Information = 0;
                Irp->IoStatus.Information |= PNP_DEVICE_FAILED;
                Irp->IoStatus.Status = STATUS_SUCCESS;
            }
            return FixtureComplete(Irp, Irp->IoStatus.Status);

        case IRP_MN_SURPRISE_REMOVAL:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            FixtureRecord(Parent, L"ParentRemoved", 1);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(Lower, Irp);
            IoDetachDevice(Lower);
            if (Parent->Child && !Parent->ChildDeleted)
            {
                Parent->ChildDeleted = TRUE;
                IoDeleteDevice(Parent->Child);
            }
            if (Parent->ChildReferenced)
            {
                Parent->ChildReferenced = FALSE;
                ObDereferenceObject(Parent->Child);
            }
            IoFreeWorkItem(Parent->WorkItem);
            IoFreeWorkItem(Parent->FailWorkItem);
            IoDeleteDevice(DeviceObject);
            return Status;
    }

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Lower, Irp);
}

static NTSTATUS NTAPI FixturePnp(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    if (((PFIXTURE_CHILD)DeviceObject->DeviceExtension)->IsFdo)
        return FixtureParentPnp(DeviceObject, Irp);
    return FixtureChildPnp(DeviceObject, Irp);
}

static NTSTATUS NTAPI FixturePower(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PFIXTURE_PARENT Parent = DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);
    if (!Parent->IsFdo)
        return FixtureComplete(Irp, STATUS_SUCCESS);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(Parent->LowerDevice, Irp);
}

static NTSTATUS NTAPI FixtureAddDevice(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT Pdo)
{
    static const WCHAR FailId[] = L"ROOT\\KMTESTREMOVECHILDFAIL";
    WCHAR HardwareIds[64];
    UNICODE_STRING Id, Expected;
    PFIXTURE_PARENT Parent;
    PDEVICE_OBJECT Fdo;
    ULONG Length = 0;
    NTSTATUS Status;

    RtlZeroMemory(HardwareIds, sizeof(HardwareIds));
    Status = IoGetDeviceProperty(Pdo, DevicePropertyHardwareID, sizeof(HardwareIds) - sizeof(WCHAR),
                                 HardwareIds, &Length);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = IoCreateDevice(DriverObject, sizeof(FIXTURE_PARENT), NULL, FILE_DEVICE_BUS_EXTENDER,
                            FILE_DEVICE_SECURE_OPEN, FALSE, &Fdo);
    if (!NT_SUCCESS(Status))
        return Status;

    Parent = Fdo->DeviceExtension;
    RtlZeroMemory(Parent, sizeof(*Parent));
    Parent->IsFdo = TRUE;
    Parent->Self = Fdo;
    Parent->Pdo = Pdo;
    RtlInitUnicodeString(&Id, HardwareIds);
    RtlInitUnicodeString(&Expected, FailId);
    Parent->FailAfterChildRemove = RtlEqualUnicodeString(&Id, &Expected, TRUE);
    RtlStringCbPrintfW(Parent->ValuePath, sizeof(Parent->ValuePath), L"%s\\%s", ServicePath,
                       Parent->FailAfterChildRemove ? L"Fail" : L"Query");
    RtlCreateRegistryKey(RTL_REGISTRY_ABSOLUTE, Parent->ValuePath);
    Parent->WorkItem = IoAllocateWorkItem(Fdo);
    Parent->FailWorkItem = IoAllocateWorkItem(Fdo);
    if (!Parent->WorkItem || !Parent->FailWorkItem)
    {
        if (Parent->WorkItem)
            IoFreeWorkItem(Parent->WorkItem);
        if (Parent->FailWorkItem)
            IoFreeWorkItem(Parent->FailWorkItem);
        IoDeleteDevice(Fdo);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Parent->LowerDevice = IoAttachDeviceToDeviceStack(Fdo, Pdo);
    if (!Parent->LowerDevice)
    {
        IoFreeWorkItem(Parent->WorkItem);
        IoFreeWorkItem(Parent->FailWorkItem);
        IoDeleteDevice(Fdo);
        return STATUS_NO_SUCH_DEVICE;
    }

    Fdo->Flags |= DO_POWER_PAGABLE;
    Fdo->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static VOID NTAPI FixtureUnload(PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
}

NTSTATUS NTAPI DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    if (RegistryPath->Length >= sizeof(ServicePath))
        return STATUS_NAME_TOO_LONG;
    RtlCopyMemory(ServicePath, RegistryPath->Buffer, RegistryPath->Length);
    ServicePath[RegistryPath->Length / sizeof(WCHAR)] = UNICODE_NULL;

    DriverObject->DriverUnload = FixtureUnload;
    DriverObject->DriverExtension->AddDevice = FixtureAddDevice;
    DriverObject->MajorFunction[IRP_MJ_PNP] = FixturePnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = FixturePower;
    return STATUS_SUCCESS;
}
