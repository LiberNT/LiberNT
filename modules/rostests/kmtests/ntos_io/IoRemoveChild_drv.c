/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bus driver whose child is reported missing and deleted while still referenced, or removed with a
 *              grandchild whose last close comes with the removal of its grandparent
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
    BOOLEAN Nested;
    PDEVICE_OBJECT Grandchild;
    HANDLE GrandchildHandle;
    BOOLEAN GrandchildPresent;
    BOOLEAN GrandchildDeleted;
    ULONG GrandchildStarts;
    ULONG GrandchildSurprises;
    ULONG GrandchildRemoves;
    ULONG GrandchildIrpsAfterDelete;
    ULONG ChildStarts;
    ULONG ChildSurprises;
    ULONG ChildRemoves;
    ULONG ChildIrpsAfterDelete;
    WCHAR ValuePath[300];
} FIXTURE_PARENT, *PFIXTURE_PARENT;

typedef struct _FIXTURE_CHILD
{
    BOOLEAN IsFdo;
    BOOLEAN IsGrandchild;
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

static VOID NTAPI FixtureOpenGrandchild(PDEVICE_OBJECT DeviceObject, PVOID Context)
{
    PFIXTURE_PARENT Parent = Context;
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\Device\\KmtestRemoveGrandchild");
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);

    InitializeObjectAttributes(&Attributes, &Name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = ZwCreateFile(&Parent->GrandchildHandle, FILE_READ_ATTRIBUTES, &Attributes, &IoStatus, NULL, 0,
                          FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN, 0, NULL, 0);
    FixtureRecord(Parent, L"GrandchildOpen", (ULONG)Status);
    if (!NT_SUCCESS(Status))
        Parent->GrandchildHandle = NULL;

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

static NTSTATUS FixtureGrandchildPnp(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PFIXTURE_PARENT Parent = ((PFIXTURE_CHILD)DeviceObject->DeviceExtension)->Parent;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    static const WCHAR DeviceId[] = L"KMTEST\\REMOVEGRANDCHILD";
    static const WCHAR HardwareIds[] = L"KMTEST\\REMOVEGRANDCHILD\0";
    static const WCHAR InstanceId[] = L"0";
    PDEVICE_CAPABILITIES Capabilities;
    PCWSTR Source;
    SIZE_T Size;
    PWSTR Buffer;

    if (Parent->GrandchildDeleted)
    {
        FixtureRecord(Parent, L"GrandchildIrpsAfterDelete", ++Parent->GrandchildIrpsAfterDelete);
        return FixtureComplete(Irp, STATUS_NO_SUCH_DEVICE);
    }

    switch (Stack->MinorFunction)
    {
        case IRP_MN_QUERY_ID:
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

        case IRP_MN_QUERY_CAPABILITIES:
            Capabilities = Stack->Parameters.DeviceCapabilities.Capabilities;
            Capabilities->RawDeviceOK = TRUE;
            Capabilities->SilentInstall = TRUE;
            Capabilities->Removable = TRUE;
            return FixtureComplete(Irp, STATUS_SUCCESS);

        case IRP_MN_START_DEVICE:
            FixtureRecord(Parent, L"GrandchildStarts", ++Parent->GrandchildStarts);
            IoQueueWorkItem(Parent->WorkItem, FixtureOpenGrandchild, DelayedWorkQueue, Parent);
            return FixtureComplete(Irp, STATUS_SUCCESS);

        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
        case IRP_MN_STOP_DEVICE:
        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
            return FixtureComplete(Irp, STATUS_SUCCESS);

        case IRP_MN_SURPRISE_REMOVAL:
            FixtureRecord(Parent, L"GrandchildSurprises", ++Parent->GrandchildSurprises);
            Parent->GrandchildPresent = FALSE;
            return FixtureComplete(Irp, STATUS_SUCCESS);

        case IRP_MN_REMOVE_DEVICE:
            FixtureRecord(Parent, L"GrandchildRemoves", ++Parent->GrandchildRemoves);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            if (!Parent->GrandchildPresent)
            {
                Parent->GrandchildDeleted = TRUE;
                IoDeleteDevice(DeviceObject);
            }
            return STATUS_SUCCESS;

        default:
            return FixtureComplete(Irp, Irp->IoStatus.Status);
    }
}

static NTSTATUS FixtureChildRelations(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PFIXTURE_PARENT Parent = ((PFIXTURE_CHILD)DeviceObject->DeviceExtension)->Parent;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PDEVICE_RELATIONS Relations;

    if (!Parent->Nested || Stack->Parameters.QueryDeviceRelations.Type != BusRelations)
        return FixtureComplete(Irp, Irp->IoStatus.Status);

    Relations = ExAllocatePoolWithTag(PagedPool, sizeof(DEVICE_RELATIONS), 'CmRK');
    if (!Relations)
        return FixtureComplete(Irp, STATUS_INSUFFICIENT_RESOURCES);
    Relations->Count = 0;
    if (Parent->GrandchildPresent)
    {
        ObReferenceObject(Parent->Grandchild);
        Relations->Objects[Relations->Count++] = Parent->Grandchild;
    }
    Irp->IoStatus.Information = (ULONG_PTR)Relations;
    return FixtureComplete(Irp, STATUS_SUCCESS);
}

static NTSTATUS FixtureCreateGrandchild(PDEVICE_OBJECT DeviceObject, PFIXTURE_PARENT Parent)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\Device\\KmtestRemoveGrandchild");
    PFIXTURE_CHILD Extension;
    NTSTATUS Status;

    Status = IoCreateDevice(DeviceObject->DriverObject, sizeof(FIXTURE_CHILD), &Name, FILE_DEVICE_UNKNOWN, 0, FALSE,
                            &Parent->Grandchild);
    if (!NT_SUCCESS(Status))
        return Status;

    Extension = Parent->Grandchild->DeviceExtension;
    Extension->IsFdo = FALSE;
    Extension->IsGrandchild = TRUE;
    Extension->Parent = Parent;
    Parent->Grandchild->Flags &= ~DO_DEVICE_INITIALIZING;
    Parent->GrandchildPresent = TRUE;
    IoInvalidateDeviceRelations(DeviceObject, BusRelations);
    return STATUS_SUCCESS;
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

        case IRP_MN_QUERY_DEVICE_RELATIONS:
            return FixtureChildRelations(DeviceObject, Irp);

        case IRP_MN_START_DEVICE:
            FixtureRecord(Parent, L"ChildStarts", ++Parent->ChildStarts);
            if (Parent->Nested)
                return FixtureComplete(Irp, FixtureCreateGrandchild(DeviceObject, Parent));
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
            if (Parent->Nested)
                IoQueueWorkItem(Parent->FailWorkItem, FixtureFailParent, DelayedWorkQueue, Parent);
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
                if (Parent->GrandchildHandle)
                {
                    ZwClose(Parent->GrandchildHandle);
                    Parent->GrandchildHandle = NULL;
                }
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
    PFIXTURE_CHILD Child = DeviceObject->DeviceExtension;

    if (Child->IsFdo)
        return FixtureParentPnp(DeviceObject, Irp);
    if (Child->IsGrandchild)
        return FixtureGrandchildPnp(DeviceObject, Irp);
    return FixtureChildPnp(DeviceObject, Irp);
}

static NTSTATUS NTAPI FixtureCreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    return FixtureComplete(Irp, STATUS_SUCCESS);
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
    static const WCHAR NestedId[] = L"ROOT\\KMTESTREMOVECHILDNESTED";
    WCHAR HardwareIds[64];
    UNICODE_STRING Id, Expected, Nested;
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
    RtlInitUnicodeString(&Nested, NestedId);
    Parent->FailAfterChildRemove = RtlEqualUnicodeString(&Id, &Expected, TRUE);
    Parent->Nested = RtlEqualUnicodeString(&Id, &Nested, TRUE);
    RtlStringCbPrintfW(Parent->ValuePath, sizeof(Parent->ValuePath), L"%s\\%s", ServicePath,
                       Parent->FailAfterChildRemove ? L"Fail" : Parent->Nested ? L"Nested" : L"Query");
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
    DriverObject->MajorFunction[IRP_MJ_CREATE] = FixtureCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = FixtureCreateClose;
    DriverObject->MajorFunction[IRP_MJ_PNP] = FixturePnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = FixturePower;
    return STATUS_SUCCESS;
}
