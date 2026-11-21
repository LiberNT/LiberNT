/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Function driver that records whether it is unloaded once the removal of its only device leaves it
 *              without a device object, and whether its queued reinitialization routine runs after that
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>

static WCHAR ServicePath[256];
static volatile LONG UnloadCalled;

static VOID WriteValue(PCWSTR Name, ULONG Value)
{
    RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, ServicePath, Name, REG_DWORD, &Value, sizeof(Value));
}

static VOID NTAPI FixtureUnload(PDRIVER_OBJECT DriverObject)
{
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    InterlockedExchange(&UnloadCalled, 1);
    WriteValue(L"DeviceLeftAtUnload", DriverObject->DeviceObject != NULL);
    WriteValue(L"Unloaded", 1);
}

static VOID NTAPI FixtureReinitialize(PDRIVER_OBJECT DriverObject, PVOID Context, ULONG Count)
{
    UNREFERENCED_PARAMETER(Context);
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    if (InterlockedCompareExchange(&UnloadCalled, 0, 0))
    {
        WriteValue(L"ReinitAfterUnload", 1);
        return;
    }
    WriteValue(L"ReinitCount", Count);
    IoRegisterDriverReinitialization(DriverObject, FixtureReinitialize, NULL);
}

static NTSTATUS NTAPI FixtureAddDevice(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT Pdo)
{
    PDEVICE_OBJECT Fdo;
    PDEVICE_OBJECT Lower;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject, sizeof(PDEVICE_OBJECT), NULL, FILE_DEVICE_UNKNOWN,
                            FILE_DEVICE_SECURE_OPEN, FALSE, &Fdo);
    if (!NT_SUCCESS(Status))
        return Status;

    Lower = IoAttachDeviceToDeviceStack(Fdo, Pdo);
    if (Lower == NULL)
    {
        IoDeleteDevice(Fdo);
        return STATUS_NO_SUCH_DEVICE;
    }

    *(PDEVICE_OBJECT *)Fdo->DeviceExtension = Lower;
    Fdo->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI FixturePnp(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PDEVICE_OBJECT Lower = *(PDEVICE_OBJECT *)DeviceObject->DeviceExtension;
    UCHAR Minor = IoGetCurrentIrpStackLocation(Irp)->MinorFunction;
    NTSTATUS Status;

    if (Minor == IRP_MN_START_DEVICE)
        WriteValue(L"Started", 1);

    if (Minor == IRP_MN_START_DEVICE || Minor == IRP_MN_QUERY_REMOVE_DEVICE || Minor == IRP_MN_CANCEL_REMOVE_DEVICE ||
        Minor == IRP_MN_SURPRISE_REMOVAL || Minor == IRP_MN_REMOVE_DEVICE || Minor == IRP_MN_QUERY_STOP_DEVICE ||
        Minor == IRP_MN_CANCEL_STOP_DEVICE || Minor == IRP_MN_STOP_DEVICE)
    {
        Irp->IoStatus.Status = STATUS_SUCCESS;
    }

    IoSkipCurrentIrpStackLocation(Irp);
    Status = IoCallDriver(Lower, Irp);

    if (Minor == IRP_MN_REMOVE_DEVICE)
    {
        IoDetachDevice(Lower);
        IoDeleteDevice(DeviceObject);
        WriteValue(L"UnloadDuringRemove", (ULONG)UnloadCalled);
    }

    return Status;
}

static NTSTATUS NTAPI FixturePower(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(*(PDEVICE_OBJECT *)DeviceObject->DeviceExtension, Irp);
}

static NTSTATUS NTAPI FixturePassDown(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(*(PDEVICE_OBJECT *)DeviceObject->DeviceExtension, Irp);
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
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = FixturePassDown;
    IoRegisterDriverReinitialization(DriverObject, FixtureReinitialize, NULL);
    WriteValue(L"Loaded", 1);
    return STATUS_SUCCESS;
}
