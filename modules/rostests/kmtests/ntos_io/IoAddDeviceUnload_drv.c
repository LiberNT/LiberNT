/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     PnP driver whose AddDevice fails after deleting its only device object
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>

NTSTATUS NTAPI IoCreateDriver(PUNICODE_STRING DriverName, PDRIVER_INITIALIZE InitializationFunction);
VOID NTAPI IoDeleteDriver(PDRIVER_OBJECT DriverObject);

static WCHAR ServicePath[260];
static volatile LONG UnloadCalled;
static volatile LONG ReinitializeCalled;
static volatile LONG AuxiliaryUnloaded;
static PDEVICE_OBJECT AuxiliaryDevice;

static VOID NTAPI AuxiliaryUnload(PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    InterlockedExchange(&AuxiliaryUnloaded, 1);
}

static VOID NTAPI AuxiliaryReinitialize(PDRIVER_OBJECT DriverObject, PVOID Context, ULONG Count)
{
    ULONG Value = Count;

    UNREFERENCED_PARAMETER(Context);
    RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, ServicePath, L"AuxiliaryReinitializeCount", REG_DWORD,
                          &Value, sizeof(Value));
    IoDeleteDriver(DriverObject);
}

static NTSTATUS NTAPI AuxiliaryInitialize(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(RegistryPath);
    DriverObject->DriverUnload = AuxiliaryUnload;
    Status = IoCreateDevice(DriverObject, 0, NULL, FILE_DEVICE_UNKNOWN, 0, FALSE, &AuxiliaryDevice);
    if (!NT_SUCCESS(Status)) return Status;
    IoRegisterDriverReinitialization(DriverObject, AuxiliaryReinitialize, NULL);
    return STATUS_SUCCESS;
}

static VOID NTAPI TestReinitialize(PDRIVER_OBJECT DriverObject, PVOID Context, ULONG Count)
{
    ULONG Value = Count;

    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(Context);
    InterlockedExchange(&ReinitializeCalled, 1);
    RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, ServicePath, L"ReinitializeCount", REG_DWORD,
                          &Value, sizeof(Value));
}

static VOID NTAPI TestUnload(PDRIVER_OBJECT DriverObject)
{
    ULONG Value = (ULONG)ReinitializeCalled;

    UNREFERENCED_PARAMETER(DriverObject);
    RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, ServicePath, L"ReinitializedBeforeUnload", REG_DWORD,
                          &Value, sizeof(Value));
    InterlockedExchange(&UnloadCalled, 1);
}

static NTSTATUS NTAPI TestAddDevice(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT Pdo)
{
    PDEVICE_OBJECT Fdo;
    ULONG UnloadDuringAddDevice;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Pdo);

    Status = IoCreateDevice(DriverObject, 0, NULL, FILE_DEVICE_UNKNOWN, 0, FALSE, &Fdo);
    if (!NT_SUCCESS(Status))
        return Status;
    IoDeleteDevice(Fdo);

    UnloadDuringAddDevice = (ULONG)UnloadCalled;
    RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, ServicePath, L"UnloadDuringAddDevice", REG_DWORD,
                          &UnloadDuringAddDevice, sizeof(UnloadDuringAddDevice));
    return STATUS_DEVICE_CONFIGURATION_ERROR;
}

NTSTATUS NTAPI DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    ULONG Value;
    NTSTATUS Status;

    if (RegistryPath->Length >= sizeof(ServicePath))
        return STATUS_NAME_TOO_LONG;
    RtlCopyMemory(ServicePath, RegistryPath->Buffer, RegistryPath->Length);
    ServicePath[RegistryPath->Length / sizeof(WCHAR)] = UNICODE_NULL;

    DriverObject->DriverUnload = TestUnload;
    DriverObject->DriverExtension->AddDevice = TestAddDevice;
    Status = IoCreateDriver(NULL, AuxiliaryInitialize);
    if (!NT_SUCCESS(Status)) return Status;
    IoDeleteDevice(AuxiliaryDevice);
    Value = (ULONG)AuxiliaryUnloaded;
    RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, ServicePath, L"AuxiliaryUnloadBeforeReinitialize", REG_DWORD,
                          &Value, sizeof(Value));
    IoRegisterDriverReinitialization(DriverObject, TestReinitialize, NULL);
    return STATUS_SUCCESS;
}
