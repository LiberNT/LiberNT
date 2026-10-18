/*
 * PROJECT:     LiberNT UEFI Bus Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Enumerates the firmware resources the EFI System Resource Table describes
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <ntstrsafe.h>
#include <initguid.h>
#include <devpkey.h>

#define NDEBUG
#include <debug.h>

#define UEFI_TAG 'IFEU'

DEFINE_DEVPROPKEY(UefiFirmwareResourceId, 0xfec439f6, 0x09a2, 0x4de5, 0xb3, 0x0f, 0x73, 0xbb, 0x39, 0x63, 0x23, 0xf5, 2);
DEFINE_DEVPROPKEY(UefiFirmwareResourceType, 0xfec439f6, 0x09a2, 0x4de5, 0xb3, 0x0f, 0x73, 0xbb, 0x39, 0x63, 0x23, 0xf5, 3);
DEFINE_DEVPROPKEY(UefiFirmwareResourceVersion, 0xfec439f6, 0x09a2, 0x4de5, 0xb3, 0x0f, 0x73, 0xbb, 0x39, 0x63, 0x23, 0xf5, 4);
DEFINE_DEVPROPKEY(UefiFirmwareResourceLowestSupportedVersion, 0xfec439f6, 0x09a2, 0x4de5, 0xb3, 0x0f, 0x73, 0xbb, 0x39, 0x63, 0x23, 0xf5, 5);
#define UEFI_GUID_CHARS 39
#define UEFI_ID_CHARS 64

typedef struct _UEFI_RESOURCE
{
    GUID FirmwareClass;
    ULONG FirmwareType;
    ULONG FirmwareVersion;
    ULONG LowestSupportedFirmwareVersion;
} UEFI_RESOURCE, *PUEFI_RESOURCE;

typedef struct _UEFI_COMMON_EXTENSION
{
    BOOLEAN IsFdo;
    PDEVICE_OBJECT Self;
} UEFI_COMMON_EXTENSION, *PUEFI_COMMON_EXTENSION;

typedef struct _UEFI_PDO_EXTENSION
{
    UEFI_COMMON_EXTENSION Common;
    UEFI_RESOURCE Resource;
    WCHAR Guid[UEFI_GUID_CHARS];
} UEFI_PDO_EXTENSION, *PUEFI_PDO_EXTENSION;

typedef struct _UEFI_FDO_EXTENSION
{
    UEFI_COMMON_EXTENSION Common;
    PDEVICE_OBJECT LowerDevice;
    IO_REMOVE_LOCK RemoveLock;
    FAST_MUTEX ChildLock;
    BOOLEAN Enumerated;
    ULONG ChildCount;
    PDEVICE_OBJECT *Children;
} UEFI_FDO_EXTENSION, *PUEFI_FDO_EXTENSION;

static
NTSTATUS
UefiCompleteRequest(
    _Inout_ PIRP Irp,
    _In_ NTSTATUS Status,
    _In_ ULONG_PTR Information)
{
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
UefiCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);
    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
UefiForwardSynchronously(
    _In_ PUEFI_FDO_EXTENSION FdoExtension,
    _In_ PIRP Irp)
{
    KEVENT Event;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, UefiCompletion, &Event, TRUE, TRUE, TRUE);
    Status = IoCallDriver(FdoExtension->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Irp->IoStatus.Status;
    }
    return Status;
}

static
NTSTATUS
UefiAllocateMultiSz(
    _In_reads_(Count) PCWSTR *Strings,
    _In_ ULONG Count,
    _In_ BOOLEAN MultiSz,
    _Out_ PWCHAR *Result)
{
    SIZE_T Chars = MultiSz ? 1 : 0;
    PWCHAR Buffer, Current;
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
        Chars += wcslen(Strings[Index]) + 1;

    Buffer = ExAllocatePoolZero(PagedPool, Chars * sizeof(WCHAR), UEFI_TAG);
    if (!Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    Current = Buffer;
    for (Index = 0; Index < Count; Index++)
    {
        RtlCopyMemory(Current, Strings[Index], wcslen(Strings[Index]) * sizeof(WCHAR));
        Current += wcslen(Strings[Index]) + 1;
    }

    *Result = Buffer;
    return STATUS_SUCCESS;
}

static
NTSTATUS
UefiQueryId(
    _In_ PUEFI_PDO_EXTENSION PdoExtension,
    _In_ BUS_QUERY_ID_TYPE IdType,
    _Out_ PWCHAR *Result)
{
    WCHAR First[UEFI_ID_CHARS], Second[UEFI_ID_CHARS];
    PCWSTR Strings[2];

    switch (IdType)
    {
        case BusQueryDeviceID:
            RtlStringCchPrintfW(First, UEFI_ID_CHARS, L"UEFI\\RES_%s", PdoExtension->Guid);
            Strings[0] = First;
            return UefiAllocateMultiSz(Strings, 1, FALSE, Result);

        case BusQueryHardwareIDs:
            RtlStringCchPrintfW(First,
                                UEFI_ID_CHARS,
                                L"UEFI\\RES_%s&REV_%lX",
                                PdoExtension->Guid,
                                PdoExtension->Resource.FirmwareVersion);
            RtlStringCchPrintfW(Second, UEFI_ID_CHARS, L"UEFI\\RES_%s", PdoExtension->Guid);
            Strings[0] = First;
            Strings[1] = Second;
            return UefiAllocateMultiSz(Strings, 2, TRUE, Result);

        case BusQueryCompatibleIDs:
            RtlStringCchPrintfW(First, UEFI_ID_CHARS, L"UEFI\\CC_0001%04lX", PdoExtension->Resource.FirmwareType);
            Strings[0] = First;
            Strings[1] = L"GenFirmwareResource";
            return UefiAllocateMultiSz(Strings, 2, TRUE, Result);

        case BusQueryInstanceID:
            Strings[0] = L"0";
            return UefiAllocateMultiSz(Strings, 1, FALSE, Result);

        default:
            return STATUS_NOT_SUPPORTED;
    }
}

static
NTSTATUS
UefiQueryCapabilities(
    _Inout_ PDEVICE_CAPABILITIES Capabilities)
{
    ULONG Index;

    if (Capabilities->Version != 1)
        return STATUS_NOT_SUPPORTED;

    Capabilities->LockSupported = FALSE;
    Capabilities->EjectSupported = FALSE;
    Capabilities->Removable = FALSE;
    Capabilities->DockDevice = FALSE;
    Capabilities->UniqueID = TRUE;
    Capabilities->SilentInstall = TRUE;
    Capabilities->RawDeviceOK = TRUE;
    Capabilities->SurpriseRemovalOK = FALSE;
    Capabilities->Address = MAXULONG;
    Capabilities->UINumber = MAXULONG;
    Capabilities->SystemWake = PowerSystemUnspecified;
    Capabilities->DeviceWake = PowerDeviceUnspecified;
    for (Index = PowerSystemWorking; Index < PowerSystemMaximum; Index++)
        Capabilities->DeviceState[Index] = PowerDeviceD3;
    Capabilities->DeviceState[PowerSystemWorking] = PowerDeviceD0;
    return STATUS_SUCCESS;
}

static
VOID
UefiSetProperty(
    _In_ PUEFI_PDO_EXTENSION PdoExtension,
    _In_ const DEVPROPKEY *PropertyKey,
    _In_ DEVPROPTYPE Type,
    _In_ ULONG Size,
    _In_reads_bytes_(Size) PVOID Data)
{
    NTSTATUS Status;

    Status = IoSetDevicePropertyData(PdoExtension->Common.Self, PropertyKey, LOCALE_NEUTRAL, 0, Type, Size, Data);
    if (!NT_SUCCESS(Status))
        DPRINT1("UEFI: property %lu failed 0x%08lx\n", PropertyKey->pid, Status);
}

static
VOID
UefiPublishProperties(
    _In_ PUEFI_PDO_EXTENSION PdoExtension)
{
    PUEFI_RESOURCE Resource = &PdoExtension->Resource;
    WCHAR Version[16];

    RtlStringCchPrintfW(Version, RTL_NUMBER_OF(Version), L"%lX", Resource->FirmwareVersion);
    UefiSetProperty(PdoExtension,
                    &DEVPKEY_Device_FirmwareVersion,
                    DEVPROP_TYPE_STRING,
                    (ULONG)((wcslen(Version) + 1) * sizeof(WCHAR)),
                    Version);
    UefiSetProperty(PdoExtension,
                    &UefiFirmwareResourceId,
                    DEVPROP_TYPE_GUID,
                    sizeof(Resource->FirmwareClass),
                    &Resource->FirmwareClass);
    UefiSetProperty(PdoExtension,
                    &UefiFirmwareResourceType,
                    DEVPROP_TYPE_UINT32,
                    sizeof(Resource->FirmwareType),
                    &Resource->FirmwareType);
    UefiSetProperty(PdoExtension,
                    &UefiFirmwareResourceVersion,
                    DEVPROP_TYPE_UINT32,
                    sizeof(Resource->FirmwareVersion),
                    &Resource->FirmwareVersion);
    UefiSetProperty(PdoExtension,
                    &UefiFirmwareResourceLowestSupportedVersion,
                    DEVPROP_TYPE_UINT32,
                    sizeof(Resource->LowestSupportedFirmwareVersion),
                    &Resource->LowestSupportedFirmwareVersion);
}

static
NTSTATUS
UefiPdoQueryDeviceRelations(
    _In_ PUEFI_PDO_EXTENSION PdoExtension,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PDEVICE_RELATIONS Relations;

    if (Stack->Parameters.QueryDeviceRelations.Type != TargetDeviceRelation)
        return Irp->IoStatus.Status;

    Relations = ExAllocatePoolWithTag(PagedPool, sizeof(DEVICE_RELATIONS), UEFI_TAG);
    if (!Relations)
        return STATUS_INSUFFICIENT_RESOURCES;

    Relations->Count = 1;
    Relations->Objects[0] = PdoExtension->Common.Self;
    ObReferenceObject(PdoExtension->Common.Self);
    Irp->IoStatus.Information = (ULONG_PTR)Relations;
    return STATUS_SUCCESS;
}

static
NTSTATUS
UefiPdoPnp(
    _In_ PUEFI_PDO_EXTENSION PdoExtension,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status = Irp->IoStatus.Status;
    PWCHAR Id;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            UefiPublishProperties(PdoExtension);
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_STOP_DEVICE:
        case IRP_MN_REMOVE_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_ID:
            Status = UefiQueryId(PdoExtension, Stack->Parameters.QueryId.IdType, &Id);
            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = (ULONG_PTR)Id;
            else if (Status == STATUS_NOT_SUPPORTED)
                Status = Irp->IoStatus.Status;
            break;

        case IRP_MN_QUERY_CAPABILITIES:
            Status = UefiQueryCapabilities(Stack->Parameters.DeviceCapabilities.Capabilities);
            break;

        case IRP_MN_QUERY_DEVICE_RELATIONS:
            Status = UefiPdoQueryDeviceRelations(PdoExtension, Irp);
            break;

        default:
            break;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
VOID
UefiFormatGuid(
    _In_ const GUID *Guid,
    _Out_writes_(UEFI_GUID_CHARS) PWCHAR Buffer)
{
    RtlStringCchPrintfW(Buffer,
                        UEFI_GUID_CHARS,
                        L"{%08lx-%04hx-%04hx-%02x%02x-%02x%02x%02x%02x%02x%02x}",
                        Guid->Data1,
                        Guid->Data2,
                        Guid->Data3,
                        Guid->Data4[0],
                        Guid->Data4[1],
                        Guid->Data4[2],
                        Guid->Data4[3],
                        Guid->Data4[4],
                        Guid->Data4[5],
                        Guid->Data4[6],
                        Guid->Data4[7]);
}

static
NTSTATUS
UefiCreateChild(
    _In_ PUEFI_FDO_EXTENSION FdoExtension,
    _In_ const UEFI_RESOURCE *Resource,
    _Out_ PDEVICE_OBJECT *Child)
{
    PUEFI_PDO_EXTENSION PdoExtension;
    PDEVICE_OBJECT DeviceObject;
    NTSTATUS Status;

    Status = IoCreateDevice(FdoExtension->Common.Self->DriverObject,
                            sizeof(*PdoExtension),
                            NULL,
                            FILE_DEVICE_UNKNOWN,
                            FILE_AUTOGENERATED_DEVICE_NAME | FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &DeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;

    PdoExtension = DeviceObject->DeviceExtension;
    RtlZeroMemory(PdoExtension, sizeof(*PdoExtension));
    PdoExtension->Common.IsFdo = FALSE;
    PdoExtension->Common.Self = DeviceObject;
    PdoExtension->Resource = *Resource;
    UefiFormatGuid(&Resource->FirmwareClass, PdoExtension->Guid);

    DeviceObject->Flags |= DO_POWER_PAGABLE;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    *Child = DeviceObject;
    return STATUS_SUCCESS;
}

static
NTSTATUS
UefiQueryDword(
    _In_ HANDLE KeyHandle,
    _In_ PCWSTR Name,
    _Out_ PULONG Value)
{
    ULONG Buffer[(sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)) / sizeof(ULONG)];
    PKEY_VALUE_PARTIAL_INFORMATION Information = (PKEY_VALUE_PARTIAL_INFORMATION)Buffer;
    UNICODE_STRING ValueName;
    ULONG Length;
    NTSTATUS Status;

    RtlInitUnicodeString(&ValueName, Name);
    Status = ZwQueryValueKey(KeyHandle, &ValueName, KeyValuePartialInformation, Information, sizeof(Buffer), &Length);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Information->Type != REG_DWORD || Information->DataLength != sizeof(ULONG))
        return STATUS_OBJECT_TYPE_MISMATCH;

    RtlCopyMemory(Value, Information->Data, sizeof(ULONG));
    return STATUS_SUCCESS;
}

static
NTSTATUS
UefiReadResource(
    _In_ HANDLE EsrtHandle,
    _In_ PUNICODE_STRING Name,
    _Out_ PUEFI_RESOURCE Resource)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE KeyHandle;
    NTSTATUS Status;

    RtlZeroMemory(Resource, sizeof(*Resource));
    Status = RtlGUIDFromString(Name, &Resource->FirmwareClass);
    if (!NT_SUCCESS(Status))
        return Status;

    InitializeObjectAttributes(&ObjectAttributes, Name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, EsrtHandle, NULL);
    Status = ZwOpenKey(&KeyHandle, KEY_READ, &ObjectAttributes);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = UefiQueryDword(KeyHandle, L"Type", &Resource->FirmwareType);
    if (NT_SUCCESS(Status))
        Status = UefiQueryDword(KeyHandle, L"Version", &Resource->FirmwareVersion);
    if (NT_SUCCESS(Status))
        Status = UefiQueryDword(KeyHandle, L"LowestSupportedVersion", &Resource->LowestSupportedFirmwareVersion);

    ZwClose(KeyHandle);
    return Status;
}

static
VOID
UefiEnumerateResources(
    _Inout_ PUEFI_FDO_EXTENSION FdoExtension)
{
    UNICODE_STRING EsrtName = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\HARDWARE\\UEFI\\ESRT");
    ULONG Buffer[(sizeof(KEY_BASIC_INFORMATION) + UEFI_GUID_CHARS * sizeof(WCHAR)) / sizeof(ULONG) + 1];
    PKEY_BASIC_INFORMATION Information = (PKEY_BASIC_INFORMATION)Buffer;
    OBJECT_ATTRIBUTES ObjectAttributes;
    KEY_FULL_INFORMATION FullInformation;
    UEFI_RESOURCE Resource;
    UNICODE_STRING Name;
    HANDLE EsrtHandle;
    ULONG Index, Length;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes, &EsrtName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwOpenKey(&EsrtHandle, KEY_READ, &ObjectAttributes);
    if (!NT_SUCCESS(Status))
        return;

    Status = ZwQueryKey(EsrtHandle, KeyFullInformation, &FullInformation, sizeof(FullInformation), &Length);
    if (!NT_SUCCESS(Status) && Status != STATUS_BUFFER_OVERFLOW)
    {
        ZwClose(EsrtHandle);
        return;
    }

    if (FullInformation.SubKeys)
    {
        FdoExtension->Children = ExAllocatePoolZero(NonPagedPool,
                                                    FullInformation.SubKeys * sizeof(PDEVICE_OBJECT),
                                                    UEFI_TAG);
    }

    for (Index = 0; FdoExtension->Children && Index < FullInformation.SubKeys; Index++)
    {
        Status = ZwEnumerateKey(EsrtHandle, Index, KeyBasicInformation, Information, sizeof(Buffer), &Length);
        if (!NT_SUCCESS(Status))
            continue;

        Name.Buffer = Information->Name;
        Name.Length = Name.MaximumLength = (USHORT)Information->NameLength;
        if (!NT_SUCCESS(UefiReadResource(EsrtHandle, &Name, &Resource)))
            continue;

        if (NT_SUCCESS(UefiCreateChild(FdoExtension, &Resource, &FdoExtension->Children[FdoExtension->ChildCount])))
            FdoExtension->ChildCount++;
    }

    ZwClose(EsrtHandle);
}

static
NTSTATUS
UefiFdoQueryBusRelations(
    _In_ PUEFI_FDO_EXTENSION FdoExtension,
    _Inout_ PIRP Irp)
{
    PDEVICE_RELATIONS Existing = (PDEVICE_RELATIONS)Irp->IoStatus.Information;
    PDEVICE_RELATIONS Relations;
    ULONG Count, Index;

    ExAcquireFastMutex(&FdoExtension->ChildLock);
    if (!FdoExtension->Enumerated)
    {
        UefiEnumerateResources(FdoExtension);
        FdoExtension->Enumerated = TRUE;
    }

    Count = FdoExtension->ChildCount + (Existing ? Existing->Count : 0);
    Relations = ExAllocatePoolWithTag(PagedPool,
                                      FIELD_OFFSET(DEVICE_RELATIONS, Objects) + Count * sizeof(PDEVICE_OBJECT),
                                      UEFI_TAG);
    if (!Relations)
    {
        ExReleaseFastMutex(&FdoExtension->ChildLock);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Relations->Count = 0;
    if (Existing)
    {
        for (Index = 0; Index < Existing->Count; Index++)
            Relations->Objects[Relations->Count++] = Existing->Objects[Index];
        ExFreePool(Existing);
    }
    for (Index = 0; Index < FdoExtension->ChildCount; Index++)
    {
        Relations->Objects[Relations->Count++] = FdoExtension->Children[Index];
        ObReferenceObject(FdoExtension->Children[Index]);
    }
    ExReleaseFastMutex(&FdoExtension->ChildLock);

    Irp->IoStatus.Information = (ULONG_PTR)Relations;
    return STATUS_SUCCESS;
}

static
VOID
UefiDeleteChildren(
    _In_ PUEFI_FDO_EXTENSION FdoExtension)
{
    ULONG Index;

    ExAcquireFastMutex(&FdoExtension->ChildLock);
    for (Index = 0; Index < FdoExtension->ChildCount; Index++)
        IoDeleteDevice(FdoExtension->Children[Index]);
    if (FdoExtension->Children)
        ExFreePoolWithTag(FdoExtension->Children, UEFI_TAG);
    FdoExtension->Children = NULL;
    FdoExtension->ChildCount = 0;
    ExReleaseFastMutex(&FdoExtension->ChildLock);
}

static
NTSTATUS
UefiFdoPnp(
    _In_ PUEFI_FDO_EXTENSION FdoExtension,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    Status = IoAcquireRemoveLock(&FdoExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
        return UefiCompleteRequest(Irp, Status, 0);

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = UefiForwardSynchronously(FdoExtension, Irp);
            IoReleaseRemoveLock(&FdoExtension->RemoveLock, Irp);
            return UefiCompleteRequest(Irp, Status, Irp->IoStatus.Information);

        case IRP_MN_QUERY_DEVICE_RELATIONS:
            if (Stack->Parameters.QueryDeviceRelations.Type != BusRelations)
                break;
            Status = UefiFdoQueryBusRelations(FdoExtension, Irp);
            if (!NT_SUCCESS(Status))
            {
                IoReleaseRemoveLock(&FdoExtension->RemoveLock, Irp);
                return UefiCompleteRequest(Irp, Status, 0);
            }
            Irp->IoStatus.Status = Status;
            break;

        case IRP_MN_REMOVE_DEVICE:
            IoReleaseRemoveLockAndWait(&FdoExtension->RemoveLock, Irp);
            UefiDeleteChildren(FdoExtension);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(FdoExtension->LowerDevice, Irp);
            IoDetachDevice(FdoExtension->LowerDevice);
            IoDeleteDevice(FdoExtension->Common.Self);
            return Status;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        default:
            break;
    }

    IoSkipCurrentIrpStackLocation(Irp);
    Status = IoCallDriver(FdoExtension->LowerDevice, Irp);
    IoReleaseRemoveLock(&FdoExtension->RemoveLock, Irp);
    return Status;
}

static
NTSTATUS
NTAPI
UefiPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PUEFI_COMMON_EXTENSION Common = DeviceObject->DeviceExtension;

    if (Common->IsFdo)
        return UefiFdoPnp((PUEFI_FDO_EXTENSION)Common, Irp);
    return UefiPdoPnp((PUEFI_PDO_EXTENSION)Common, Irp);
}

static
NTSTATUS
NTAPI
UefiPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PUEFI_COMMON_EXTENSION Common = DeviceObject->DeviceExtension;
    PUEFI_FDO_EXTENSION FdoExtension;
    PIO_STACK_LOCATION Stack;
    NTSTATUS Status;

    PoStartNextPowerIrp(Irp);
    if (!Common->IsFdo)
    {
        Stack = IoGetCurrentIrpStackLocation(Irp);
        Status = Irp->IoStatus.Status;
        if (Stack->MinorFunction == IRP_MN_SET_POWER || Stack->MinorFunction == IRP_MN_QUERY_POWER)
            Status = STATUS_SUCCESS;
        return UefiCompleteRequest(Irp, Status, Irp->IoStatus.Information);
    }

    FdoExtension = (PUEFI_FDO_EXTENSION)Common;
    Status = IoAcquireRemoveLock(&FdoExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
        return UefiCompleteRequest(Irp, Status, 0);
    IoSkipCurrentIrpStackLocation(Irp);
    Status = PoCallDriver(FdoExtension->LowerDevice, Irp);
    IoReleaseRemoveLock(&FdoExtension->RemoveLock, Irp);
    return Status;
}

static
NTSTATUS
NTAPI
UefiSystemControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PUEFI_COMMON_EXTENSION Common = DeviceObject->DeviceExtension;
    PUEFI_FDO_EXTENSION FdoExtension;
    NTSTATUS Status;

    if (!Common->IsFdo)
    {
        Status = Irp->IoStatus.Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    FdoExtension = (PUEFI_FDO_EXTENSION)Common;
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(FdoExtension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
UefiAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PUEFI_FDO_EXTENSION FdoExtension;
    PDEVICE_OBJECT DeviceObject;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject,
                            sizeof(*FdoExtension),
                            NULL,
                            FILE_DEVICE_BUS_EXTENDER,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &DeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;

    FdoExtension = DeviceObject->DeviceExtension;
    RtlZeroMemory(FdoExtension, sizeof(*FdoExtension));
    FdoExtension->Common.IsFdo = TRUE;
    FdoExtension->Common.Self = DeviceObject;
    IoInitializeRemoveLock(&FdoExtension->RemoveLock, UEFI_TAG, 0, 0);
    ExInitializeFastMutex(&FdoExtension->ChildLock);

    Status = IoAttachDeviceToDeviceStackSafe(DeviceObject, PhysicalDeviceObject, &FdoExtension->LowerDevice);
    if (!NT_SUCCESS(Status))
    {
        IoDeleteDevice(DeviceObject);
        return Status;
    }

    DeviceObject->Flags |= DO_POWER_PAGABLE;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    DriverObject->MajorFunction[IRP_MJ_PNP] = UefiPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = UefiPower;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = UefiSystemControl;
    DriverObject->DriverExtension->AddDevice = UefiAddDevice;
    return STATUS_SUCCESS;
}
