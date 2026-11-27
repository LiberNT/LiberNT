/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Function driver of a software device that exposes an interface to open and counts its PnP requests
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <ntddk.h>
#include <initguid.h>

DEFINE_GUID(GUID_KMTEST_SWDEVICE_INUSE, 0x765f3e7f, 0x1406, 0x49c6, 0xb7, 0x44, 0x3e, 0x98, 0x67, 0xf2, 0x99, 0xa8);

typedef struct _FIXTURE_EXTENSION
{
    PDEVICE_OBJECT Lower;
    UNICODE_STRING Interface;
    BOOLEAN Removed;
} FIXTURE_EXTENSION, *PFIXTURE_EXTENSION;

static WCHAR ServicePath[256];
static FAST_MUTEX CountLock;

static VOID Count(PCWSTR Name)
{
    ULONG Value = 0;
    RTL_QUERY_REGISTRY_TABLE Query[2];

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    ExAcquireFastMutex(&CountLock);
    RtlZeroMemory(Query, sizeof(Query));
    Query[0].Flags = RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_TYPECHECK;
    Query[0].Name = (PWSTR)Name;
    Query[0].EntryContext = &Value;
    Query[0].DefaultType = (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_NONE;
    if (!NT_SUCCESS(RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE, ServicePath, Query, NULL, NULL)))
        Value = 0;
    Value++;
    RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, ServicePath, Name, REG_DWORD, &Value, sizeof(Value));
    ExReleaseFastMutex(&CountLock);
}

static NTSTATUS NTAPI FixtureAddDevice(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT Pdo)
{
    PFIXTURE_EXTENSION Extension;
    PDEVICE_OBJECT Fdo;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject, sizeof(FIXTURE_EXTENSION), NULL, FILE_DEVICE_UNKNOWN,
                            FILE_DEVICE_SECURE_OPEN, FALSE, &Fdo);
    if (!NT_SUCCESS(Status))
        return Status;

    Extension = Fdo->DeviceExtension;
    RtlZeroMemory(Extension, sizeof(*Extension));
    Status = IoRegisterDeviceInterface(Pdo, &GUID_KMTEST_SWDEVICE_INUSE, NULL, &Extension->Interface);
    if (!NT_SUCCESS(Status))
    {
        IoDeleteDevice(Fdo);
        return Status;
    }

    Extension->Lower = IoAttachDeviceToDeviceStack(Fdo, Pdo);
    if (Extension->Lower == NULL)
    {
        RtlFreeUnicodeString(&Extension->Interface);
        IoDeleteDevice(Fdo);
        return STATUS_NO_SUCH_DEVICE;
    }

    Fdo->Flags &= ~DO_DEVICE_INITIALIZING;
    Count(L"Adds");
    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI FixturePnp(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PFIXTURE_EXTENSION Extension = DeviceObject->DeviceExtension;
    UCHAR Minor = IoGetCurrentIrpStackLocation(Irp)->MinorFunction;
    PDEVICE_OBJECT Lower = Extension->Lower;
    NTSTATUS Status;

    switch (Minor)
    {
        case IRP_MN_START_DEVICE:
            Status = IoForwardIrpSynchronously(Lower, Irp) ? Irp->IoStatus.Status : STATUS_UNSUCCESSFUL;
            if (NT_SUCCESS(Status))
            {
                if (!NT_SUCCESS(IoSetDeviceInterfaceState(&Extension->Interface, TRUE)))
                    Count(L"InterfaceFailures");
                Count(L"Starts");
            }
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_SURPRISE_REMOVAL:
            Extension->Removed = TRUE;
            Status = IoSetDeviceInterfaceState(&Extension->Interface, FALSE);
            Count(L"Surprises");
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            if (!Extension->Removed)
                Status = IoSetDeviceInterfaceState(&Extension->Interface, FALSE);
            Count(L"Removes");
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(Lower, Irp);
            IoDetachDevice(Lower);
            RtlFreeUnicodeString(&Extension->Interface);
            IoDeleteDevice(DeviceObject);
            return Status;

        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_STOP_DEVICE:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;
    }

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Lower, Irp);
}

static NTSTATUS NTAPI FixtureCreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    if (IoGetCurrentIrpStackLocation(Irp)->MajorFunction == IRP_MJ_CREATE)
        Count(L"Opens");
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI FixturePower(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(((PFIXTURE_EXTENSION)DeviceObject->DeviceExtension)->Lower, Irp);
}

static NTSTATUS NTAPI FixturePassDown(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(((PFIXTURE_EXTENSION)DeviceObject->DeviceExtension)->Lower, Irp);
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
    ExInitializeFastMutex(&CountLock);

    DriverObject->DriverUnload = FixtureUnload;
    DriverObject->DriverExtension->AddDevice = FixtureAddDevice;
    DriverObject->MajorFunction[IRP_MJ_PNP] = FixturePnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = FixturePower;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = FixturePassDown;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = FixtureCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = FixtureCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = FixtureCreateClose;
    return STATUS_SUCCESS;
}
