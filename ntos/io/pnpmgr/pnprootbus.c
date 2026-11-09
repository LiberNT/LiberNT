/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The root enumerator, the bus every other bus hangs from
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

typedef struct _PNPROOT_DEVICE
{
    LIST_ENTRY ListEntry;
    UNICODE_STRING DeviceId;
    UNICODE_STRING InstanceId;
    PDEVICE_OBJECT PhysicalDeviceObject;
    PCM_RESOURCE_LIST ResourceList;
    ULONG ResourceListSize;
} PNPROOT_DEVICE, *PPNPROOT_DEVICE;

typedef struct _PNPROOT_EXTENSION
{
    LIST_ENTRY DeviceListHead;
    ULONG DeviceListCount;
    KGUARDED_MUTEX Lock;
} PNPROOT_EXTENSION, *PPNPROOT_EXTENSION;

typedef struct _PNPROOT_PDO_EXTENSION
{
    PPNPROOT_DEVICE Device;
} PNPROOT_PDO_EXTENSION, *PPNPROOT_PDO_EXTENSION;

static PDEVICE_OBJECT PnpRootDeviceObject = NULL;
static PDRIVER_OBJECT PnpRootDriverObject = NULL;
static PNPROOT_EXTENSION PnpRootExtension;

static const UNICODE_STRING PnpRootDeviceName =
    RTL_CONSTANT_STRING(L"\\Device\\PnpRoot");

static const UNICODE_STRING PnpRootDriverName =
    RTL_CONSTANT_STRING(L"\\Driver\\PnpManager");

static const UNICODE_STRING PnpRootEnumerator =
    RTL_CONSTANT_STRING(L"ROOT");

/* PRIVATE FUNCTIONS **********************************************************/

VOID
PnpRootInitializeDevExtension(VOID)
{
    InitializeListHead(&PnpRootExtension.DeviceListHead);
    PnpRootExtension.DeviceListCount = 0;
    KeInitializeGuardedMutex(&PnpRootExtension.Lock);
}

static
NTSTATUS
PnpRootDuplicateString(
    _In_ PCUNICODE_STRING Source,
    _Out_ PUNICODE_STRING Target)
{
    PWCHAR Buffer;

    RtlInitEmptyUnicodeString(Target, NULL, 0);

    Buffer = ExAllocatePoolZero(PagedPool,
                                Source->Length + sizeof(WCHAR),
                                TAG_PNP_ROOT);
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(Buffer, Source->Buffer, Source->Length);

    Target->Buffer = Buffer;
    Target->Length = Source->Length;
    Target->MaximumLength = Source->Length + sizeof(WCHAR);

    return STATUS_SUCCESS;
}

NTSTATUS
PnpRootCreateDeviceObject(
    OUT PDEVICE_OBJECT *DeviceObject)
{
    NTSTATUS Status;

    PAGED_CODE();

    *DeviceObject = NULL;

    if (PnpRootDeviceObject != NULL)
    {
        *DeviceObject = PnpRootDeviceObject;
        return STATUS_SUCCESS;
    }

    if (PnpRootDriverObject == NULL) return STATUS_UNSUCCESSFUL;

    Status = IoCreateDevice(PnpRootDriverObject,
                            0,
                            (PUNICODE_STRING)&PnpRootDeviceName,
                            FILE_DEVICE_CONTROLLER,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &PnpRootDeviceObject);
    if (!NT_SUCCESS(Status)) return Status;

    PnpRootDeviceObject->Flags |= DO_BUS_ENUMERATED_DEVICE;

    *DeviceObject = PnpRootDeviceObject;

    return STATUS_SUCCESS;
}

NTSTATUS
PnpRootCreateDevice(
    IN PUNICODE_STRING ServiceName,
    OUT PDEVICE_OBJECT *PhysicalDeviceObject,
    OUT PUNICODE_STRING FullInstancePath)
{
    PPNPROOT_PDO_EXTENSION PdoExtension;
    PDEVICE_OBJECT Pdo = NULL;
    PPNPROOT_DEVICE Device;
    UNICODE_STRING DeviceId;
    UNICODE_STRING Instance;
    WCHAR InstanceBuffer[16];
    NTSTATUS Status;
    USHORT Length;
    PWCHAR Buffer;
    ULONG Index;

    PAGED_CODE();

    if (FullInstancePath != NULL)
    {
        RtlInitEmptyUnicodeString(FullInstancePath, NULL, 0);
    }

    Length = PnpRootEnumerator.Length + sizeof(WCHAR) + ServiceName->Length +
             sizeof(WCHAR);

    Buffer = ExAllocatePoolZero(PagedPool, Length, TAG_PNP_ROOT);
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    DeviceId.Buffer = Buffer;
    DeviceId.Length = 0;
    DeviceId.MaximumLength = Length;

    RtlAppendUnicodeStringToString(&DeviceId,
                                   (PUNICODE_STRING)&PnpRootEnumerator);
    RtlAppendUnicodeToString(&DeviceId, L"\\");
    RtlAppendUnicodeStringToString(&DeviceId, ServiceName);

    Device = ExAllocatePoolZero(PagedPool,
                                sizeof(PNPROOT_DEVICE),
                                TAG_PNP_ROOT);
    if (Device == NULL)
    {
        ExFreePoolWithTag(Buffer, TAG_PNP_ROOT);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Device->DeviceId = DeviceId;

    KeAcquireGuardedMutex(&PnpRootExtension.Lock);
    Index = PnpRootExtension.DeviceListCount;
    KeReleaseGuardedMutex(&PnpRootExtension.Lock);

    Instance.Buffer = InstanceBuffer;
    Instance.Length = 0;
    Instance.MaximumLength = sizeof(InstanceBuffer);

    Status = RtlIntegerToUnicodeString(Index, 10, &Instance);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Device, TAG_PNP_ROOT);
        ExFreePoolWithTag(Buffer, TAG_PNP_ROOT);
        return Status;
    }

    Status = PnpRootDuplicateString(&Instance, &Device->InstanceId);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Device, TAG_PNP_ROOT);
        ExFreePoolWithTag(Buffer, TAG_PNP_ROOT);
        return Status;
    }

    Status = IoCreateDevice(PnpRootDriverObject,
                            sizeof(PNPROOT_PDO_EXTENSION),
                            NULL,
                            FILE_DEVICE_CONTROLLER,
                            FILE_AUTOGENERATED_DEVICE_NAME,
                            FALSE,
                            &Pdo);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Device->InstanceId.Buffer, TAG_PNP_ROOT);
        ExFreePoolWithTag(Device, TAG_PNP_ROOT);
        ExFreePoolWithTag(Buffer, TAG_PNP_ROOT);
        return Status;
    }

    PdoExtension = Pdo->DeviceExtension;
    PdoExtension->Device = Device;

    Device->PhysicalDeviceObject = Pdo;

    Pdo->Flags |= DO_BUS_ENUMERATED_DEVICE;
    Pdo->Flags &= ~DO_DEVICE_INITIALIZING;

    KeAcquireGuardedMutex(&PnpRootExtension.Lock);
    InsertTailList(&PnpRootExtension.DeviceListHead, &Device->ListEntry);
    PnpRootExtension.DeviceListCount++;
    KeReleaseGuardedMutex(&PnpRootExtension.Lock);

    if (FullInstancePath != NULL)
    {
        Length = Device->DeviceId.Length + sizeof(WCHAR) +
                 Device->InstanceId.Length + sizeof(WCHAR);

        Buffer = ExAllocatePoolZero(PagedPool, Length, TAG_PNP_ROOT);
        if (Buffer == NULL)
        {
            *PhysicalDeviceObject = Pdo;
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        FullInstancePath->Buffer = Buffer;
        FullInstancePath->Length = 0;
        FullInstancePath->MaximumLength = Length;

        RtlAppendUnicodeStringToString(FullInstancePath, &Device->DeviceId);
        RtlAppendUnicodeToString(FullInstancePath, L"\\");
        RtlAppendUnicodeStringToString(FullInstancePath,
                                       &Device->InstanceId);
    }

    *PhysicalDeviceObject = Pdo;

    return STATUS_SUCCESS;
}

NTSTATUS
PnpRootRegisterDevice(
    IN PDEVICE_OBJECT DeviceObject)
{
    PPNPROOT_PDO_EXTENSION PdoExtension;

    PAGED_CODE();

    if (DeviceObject->DriverObject != PnpRootDriverObject)
    {
        return STATUS_INVALID_PARAMETER;
    }

    PdoExtension = DeviceObject->DeviceExtension;
    if (PdoExtension == NULL || PdoExtension->Device == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    KeAcquireGuardedMutex(&PnpRootExtension.Lock);

    if (IsListEmpty(&PdoExtension->Device->ListEntry))
    {
        InsertTailList(&PnpRootExtension.DeviceListHead,
                       &PdoExtension->Device->ListEntry);
        PnpRootExtension.DeviceListCount++;
    }

    KeReleaseGuardedMutex(&PnpRootExtension.Lock);

    return STATUS_SUCCESS;
}

VOID
PnpRootRetireDevice(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PPNPROOT_PDO_EXTENSION PdoExtension;
    PPNPROOT_DEVICE Device;

    PAGED_CODE();

    if (DeviceObject->DriverObject != PnpRootDriverObject) return;

    PdoExtension = DeviceObject->DeviceExtension;
    if (PdoExtension == NULL || PdoExtension->Device == NULL) return;

    Device = PdoExtension->Device;
    PdoExtension->Device = NULL;

    KeAcquireGuardedMutex(&PnpRootExtension.Lock);

    RemoveEntryList(&Device->ListEntry);
    InitializeListHead(&Device->ListEntry);

    if (PnpRootExtension.DeviceListCount != 0)
    {
        PnpRootExtension.DeviceListCount--;
    }

    KeReleaseGuardedMutex(&PnpRootExtension.Lock);

    if (Device->DeviceId.Buffer != NULL)
    {
        ExFreePoolWithTag(Device->DeviceId.Buffer, TAG_PNP_ROOT);
    }

    if (Device->InstanceId.Buffer != NULL)
    {
        ExFreePoolWithTag(Device->InstanceId.Buffer, TAG_PNP_ROOT);
    }

    if (Device->ResourceList != NULL)
    {
        ExFreePoolWithTag(Device->ResourceList, TAG_PNP_ROOT);
    }

    ExFreePoolWithTag(Device, TAG_PNP_ROOT);
}

NTSTATUS
NTAPI
PnpRootAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(PhysicalDeviceObject);

    return STATUS_SUCCESS;
}

static
NTSTATUS
PnpRootQueryId(
    _In_ PPNPROOT_DEVICE Device,
    _In_ BUS_QUERY_ID_TYPE IdType,
    _Out_ PWSTR *Id)
{
    PWCHAR Buffer;
    USHORT Length;

    *Id = NULL;

    switch (IdType)
    {
        case BusQueryDeviceID:
            Length = Device->DeviceId.Length + sizeof(WCHAR);
            break;

        case BusQueryInstanceID:
            Length = Device->InstanceId.Length + sizeof(WCHAR);
            break;

        case BusQueryHardwareIDs:
            Length = Device->DeviceId.Length + 2 * sizeof(WCHAR);
            break;

        default:
            return STATUS_NOT_SUPPORTED;
    }

    Buffer = ExAllocatePoolZero(PagedPool, Length, TAG_PNP_ROOT);
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    if (IdType == BusQueryInstanceID)
    {
        RtlCopyMemory(Buffer,
                      Device->InstanceId.Buffer,
                      Device->InstanceId.Length);
    }
    else
    {
        RtlCopyMemory(Buffer,
                      Device->DeviceId.Buffer,
                      Device->DeviceId.Length);
    }

    *Id = Buffer;

    return STATUS_SUCCESS;
}

static
NTSTATUS
PnpRootQueryDeviceRelations(
    _In_ PIRP Irp)
{
    PDEVICE_RELATIONS Relations;
    PPNPROOT_DEVICE Device;
    PLIST_ENTRY Entry;
    ULONG Count;
    ULONG Index = 0;
    ULONG Size;

    PAGED_CODE();

    KeAcquireGuardedMutex(&PnpRootExtension.Lock);

    Count = PnpRootExtension.DeviceListCount;

    Size = (ULONG)(FIELD_OFFSET(DEVICE_RELATIONS, Objects) +
                   Count * sizeof(PDEVICE_OBJECT));

    Relations = ExAllocatePoolZero(PagedPool, Size, TAG_PNP_ROOT);
    if (Relations == NULL)
    {
        KeReleaseGuardedMutex(&PnpRootExtension.Lock);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Entry = PnpRootExtension.DeviceListHead.Flink;
    while (Entry != &PnpRootExtension.DeviceListHead && Index < Count)
    {
        Device = CONTAINING_RECORD(Entry, PNPROOT_DEVICE, ListEntry);
        Entry = Entry->Flink;

        if (Device->PhysicalDeviceObject == NULL) continue;

        ObReferenceObject(Device->PhysicalDeviceObject);
        Relations->Objects[Index] = Device->PhysicalDeviceObject;
        Index++;
    }

    Relations->Count = Index;

    KeReleaseGuardedMutex(&PnpRootExtension.Lock);

    Irp->IoStatus.Information = (ULONG_PTR)Relations;

    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
PnpRootPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION StackPtr = IoGetCurrentIrpStackLocation(Irp);
    PPNPROOT_PDO_EXTENSION PdoExtension;
    NTSTATUS Status = Irp->IoStatus.Status;
    PWSTR Id = NULL;

    PAGED_CODE();

    if (DeviceObject == PnpRootDeviceObject)
    {
        switch (StackPtr->MinorFunction)
        {
            case IRP_MN_QUERY_DEVICE_RELATIONS:
                if (StackPtr->Parameters.QueryDeviceRelations.Type !=
                    BusRelations)
                {
                    break;
                }

                Status = PnpRootQueryDeviceRelations(Irp);
                break;

            case IRP_MN_START_DEVICE:
            case IRP_MN_QUERY_REMOVE_DEVICE:
            case IRP_MN_REMOVE_DEVICE:
            case IRP_MN_CANCEL_REMOVE_DEVICE:
            case IRP_MN_STOP_DEVICE:
            case IRP_MN_QUERY_STOP_DEVICE:
            case IRP_MN_CANCEL_STOP_DEVICE:
            case IRP_MN_SURPRISE_REMOVAL:
                Status = STATUS_SUCCESS;
                break;

            default:
                break;
        }

        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);

        return Status;
    }

    PdoExtension = DeviceObject->DeviceExtension;

    switch (StackPtr->MinorFunction)
    {
        case IRP_MN_QUERY_ID:
            if (PdoExtension == NULL || PdoExtension->Device == NULL)
            {
                Status = STATUS_NO_SUCH_DEVICE;
                break;
            }

            Status = PnpRootQueryId(PdoExtension->Device,
                                    StackPtr->Parameters.QueryId.IdType,
                                    &Id);
            if (NT_SUCCESS(Status)) Irp->IoStatus.Information = (ULONG_PTR)Id;
            break;

        case IRP_MN_QUERY_CAPABILITIES:
        {
            PDEVICE_CAPABILITIES Capabilities =
                StackPtr->Parameters.DeviceCapabilities.Capabilities;

            if (Capabilities->Version != 1)
            {
                Status = STATUS_UNSUCCESSFUL;
                break;
            }

            Capabilities->UniqueID = TRUE;
            Capabilities->SilentInstall = TRUE;
            Capabilities->RawDeviceOK = FALSE;
            Capabilities->Address = (ULONG)-1;
            Capabilities->UINumber = (ULONG)-1;

            Status = STATUS_SUCCESS;
            break;
        }

        case IRP_MN_QUERY_RESOURCES:
            if (PdoExtension == NULL ||
                PdoExtension->Device == NULL ||
                PdoExtension->Device->ResourceList == NULL)
            {
                Status = STATUS_NOT_SUPPORTED;
                break;
            }

            Irp->IoStatus.Information =
                (ULONG_PTR)PdoExtension->Device->ResourceList;
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_DEVICE_RELATIONS:
            if (StackPtr->Parameters.QueryDeviceRelations.Type !=
                TargetDeviceRelation)
            {
                break;
            }

            {
                PDEVICE_RELATIONS Relations;

                Relations = ExAllocatePoolZero(PagedPool,
                                               sizeof(DEVICE_RELATIONS),
                                               TAG_PNP_ROOT);
                if (Relations == NULL)
                {
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                    break;
                }

                ObReferenceObject(DeviceObject);

                Relations->Count = 1;
                Relations->Objects[0] = DeviceObject;

                Irp->IoStatus.Information = (ULONG_PTR)Relations;
                Status = STATUS_SUCCESS;
            }
            break;

        case IRP_MN_START_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
        case IRP_MN_STOP_DEVICE:
        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            PnpRootRetireDevice(DeviceObject);
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_PNP_DEVICE_STATE:
            Status = STATUS_SUCCESS;
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
NTAPI
PnpRootPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION StackPtr = IoGetCurrentIrpStackLocation(Irp);

    UNREFERENCED_PARAMETER(DeviceObject);

    if (StackPtr->MinorFunction == IRP_MN_SET_POWER ||
        StackPtr->MinorFunction == IRP_MN_QUERY_POWER)
    {
        Irp->IoStatus.Status = STATUS_SUCCESS;
    }

    PoStartNextPowerIrp(Irp);
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Irp->IoStatus.Status;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
PnpRootDriverEntry(
   IN PDRIVER_OBJECT DriverObject,
   IN PUNICODE_STRING RegistryPath)
{
    ULONG Index;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(RegistryPath);

    if (DriverObject == NULL)
    {
        return IoCreateDriver((PUNICODE_STRING)&PnpRootDriverName,
                              PnpRootDriverEntry);
    }

    PnpRootDriverObject = DriverObject;
    IopRootDriverObject = DriverObject;

    for (Index = 0; Index <= IRP_MJ_MAXIMUM_FUNCTION; Index++)
    {
        DriverObject->MajorFunction[Index] = IopInvalidDeviceRequest;
    }

    DriverObject->MajorFunction[IRP_MJ_PNP] = PnpRootPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = PnpRootPower;

    IoGetDrvObjExtension(DriverObject)->AddDevice = PnpRootAddDevice;

    return STATUS_SUCCESS;
}
